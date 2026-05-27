/*
 * bb_audio_stream.c - Real-time audio stream manager
 *
 * Full-duplex audio pipeline for i.MX95:
 *   capture_thread -> SPSC pool -> codec_thread -> MPSC pool -> playback_thread
 *
 * Zero-copy via bb_pool. RT priority via bb_thread.
 * PCM pass-through when codec_ops=NULL.
 */
#include "bb_audio_stream.h"
#include "bb_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

static int xrun_recover(int fd, const char *who) {
    bb_log_warn("%s: xrun recovered", who);
    return ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, 0);
}

static inline uint32_t frames_to_bytes(uint32_t frames, uint32_t ch, bb_audio_fmt_t fmt) {
    switch (fmt) {
    case BB_AUDIO_FMT_S16_LE: return frames * ch * 2;
    case BB_AUDIO_FMT_S24_LE: return frames * ch * 3;
    case BB_AUDIO_FMT_S32_LE: return frames * ch * 4;
    default: return frames * ch * 2;
    }
}

typedef struct {
    bb_audio_stream_t *s;
    int                xrun_count;
} thread_ctx_t;

static void *capture_thread_fn(void *arg) {
    thread_ctx_t *ctx = arg;
    bb_audio_stream_t *s = ctx->s;

    bb_thread_set_rt(s->cfg.rt_priority);
    bb_thread_pin(s->cfg.cpu_capture);

    bb_log_info("capture thread started (cpu=%d prio=%d)",
                s->cfg.cpu_capture, s->cfg.rt_priority);

    uint32_t period_frames = s->cap_dev.period_frames;
    uint32_t period_bytes  = s->cap_dev.period_bytes;

    while (atomic_load(&s->running)) {
        bb_frame_t *f = bb_pool_sp_acquire(&s->sp_pool);
        if (!f) {
            atomic_fetch_add(&s->drop_frames, period_frames);
            uint8_t discard[period_bytes];
            int n = bb_audio_read(&s->cap_dev, discard, period_frames);
            if (n < 0) {
                if (n == -EPIPE) {
                    if (++ctx->xrun_count > 10) {
                        bb_log_error("capture: too many xruns");
                        atomic_store(&s->error, true);
                        break;
                    }
                    xrun_recover(s->cap_dev.fd, "capture");
                }
            }
            ctx->xrun_count = 0;
            usleep(1000);
            continue;
        }

        int n = bb_audio_read(&s->cap_dev, f->data, period_frames);
        if (n < 0) {
            bb_pool_sp_release(f);
            if (n == -EPIPE) {
                if (++ctx->xrun_count > 10) {
                    bb_log_error("capture: too many xruns");
                    atomic_store(&s->error, true);
                    break;
                }
                xrun_recover(s->cap_dev.fd, "capture");
                continue;
            }
            bb_log_error("capture read: %d", n);
            atomic_store(&s->error, true);
            break;
        }
        ctx->xrun_count = 0;

        f->size = frames_to_bytes((uint32_t)n, s->cfg.channels, s->cfg.fmt);
        atomic_fetch_add(&s->cap_frames, (uint32_t)n);
        bb_pool_sp_commit(&s->sp_pool);
    }

    bb_log_info("capture thread exit");
    free(ctx);
    return NULL;
}

static void *codec_thread_fn(void *arg) {
    thread_ctx_t *ctx = arg;
    bb_audio_stream_t *s = ctx->s;

    bb_thread_set_rt(s->cfg.rt_priority);
    bb_thread_pin(s->cfg.cpu_codec);

    bb_log_info("codec thread started (cpu=%d)", s->cfg.cpu_codec);

    int has_codec = (s->cfg.codec_ops != NULL);

    while (atomic_load(&s->running)) {
        bb_frame_t *sp_f = bb_pool_sp_dequeue(&s->sp_pool);
        if (!sp_f) {
            usleep(1000);
            continue;
        }

        bb_frame_t *mp_f = bb_pool_mp_acquire(&s->mp_pool);
        if (!mp_f) {
            atomic_fetch_add(&s->drop_frames, s->cap_dev.period_frames);
            bb_pool_sp_release(sp_f);
            continue;
        }

        if (has_codec) {
            size_t out_len = 0;
            int ret = s->cfg.codec_ops->encode(s->codec_ctx,
                                               sp_f->data, sp_f->size,
                                               mp_f->data, mp_f->size,
                                               &out_len);
            if (ret == 0) {
                mp_f->size = out_len;
            } else {
                mp_f->size = 0;
            }
        } else {
            memcpy(mp_f->data, sp_f->data, sp_f->size);
            mp_f->size = sp_f->size;
        }

        mp_f->pts       = sp_f->pts;
        mp_f->frame_num = sp_f->frame_num;

        bb_pool_sp_release(sp_f);
        bb_pool_mp_enqueue(&s->mp_pool, mp_f);
    }

    // Drain remaining SPSC frames
    bb_frame_t *sp_f;
    while ((sp_f = bb_pool_sp_dequeue(&s->sp_pool)) != NULL) {
        bb_pool_sp_release(sp_f);
    }

    // Send sentinel to unblock playback thread
    for (int retry = 0; retry < 100; retry++) {
        bb_frame_t *sentinel = bb_pool_mp_acquire(&s->mp_pool);
        if (sentinel) {
            sentinel->size = 0;
            bb_pool_mp_enqueue(&s->mp_pool, sentinel);
            break;
        }
        usleep(1000);
    }

    bb_log_info("codec thread exit");
    free(ctx);
    return NULL;
}

static void *playback_thread_fn(void *arg) {
    thread_ctx_t *ctx = arg;
    bb_audio_stream_t *s = ctx->s;

    bb_thread_set_rt(s->cfg.rt_priority);
    bb_thread_pin(s->cfg.cpu_playback);

    bb_log_info("playback thread started (cpu=%d prio=%d)",
                s->cfg.cpu_playback, s->cfg.rt_priority);

    while (atomic_load(&s->running)) {
        bb_frame_t *f = bb_pool_mp_dequeue(&s->mp_pool);
        if (!f) continue;

        if (f->size == 0) {
            bb_pool_mp_release(f);
            break;
        }

        uint32_t frames = f->size / (s->cfg.channels *
                        (s->cfg.fmt == BB_AUDIO_FMT_S16_LE ? 2 :
                         s->cfg.fmt == BB_AUDIO_FMT_S24_LE ? 3 : 4));

        int n = bb_audio_write(&s->pb_dev, f->data, frames);
        if (n < 0) {
            if (n == -EPIPE) {
                if (++ctx->xrun_count > 10) {
                    bb_log_error("playback: too many xruns");
                    atomic_store(&s->error, true);
                    bb_pool_mp_release(f);
                    break;
                }
                xrun_recover(s->pb_dev.fd, "playback");
            } else {
                bb_log_error("playback write: %d", n);
                atomic_store(&s->error, true);
                bb_pool_mp_release(f);
                break;
            }
        } else {
            ctx->xrun_count = 0;
            atomic_fetch_add(&s->pb_frames, (uint32_t)n);
        }

        bb_pool_mp_release(f);
    }

    bb_log_info("playback thread exit");
    free(ctx);
    return NULL;
}

int bb_audio_stream_init(bb_audio_stream_t *s, const bb_audio_stream_config_t *cfg) {
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;

    if (bb_audio_open(&s->cap_dev, cfg->card, cfg->dev, BB_AUDIO_CAPTURE,
                      cfg->rate, cfg->channels, cfg->fmt) < 0) {
        bb_log_error("stream: capture open failed");
        return -ENODEV;
    }

    if (bb_audio_open(&s->pb_dev, cfg->card, cfg->dev, BB_AUDIO_PLAYBACK,
                      cfg->rate, cfg->channels, cfg->fmt) < 0) {
        bb_log_error("stream: playback open failed");
        bb_audio_close(&s->cap_dev);
        return -ENODEV;
    }

    if (s->cap_dev.period_frames != s->pb_dev.period_frames) {
        bb_log_error("stream: period mismatch cap=%u pb=%u",
                     s->cap_dev.period_frames, s->pb_dev.period_frames);
        bb_audio_close(&s->cap_dev);
        bb_audio_close(&s->pb_dev);
        return -EINVAL;
    }

    size_t frame_size = s->cap_dev.period_bytes;
    int nframes = cfg->pool_frames > 0 ? cfg->pool_frames : 4;

    if (bb_pool_init(&s->sp_pool, nframes, frame_size, 0) < 0) {
        bb_log_error("stream: SPSC pool init failed");
        bb_audio_close(&s->cap_dev);
        bb_audio_close(&s->pb_dev);
        return -ENOMEM;
    }

    if (bb_pool_init(&s->mp_pool, nframes, frame_size, 0) < 0) {
        bb_log_error("stream: MPSC pool init failed");
        bb_pool_destroy(&s->sp_pool);
        bb_audio_close(&s->cap_dev);
        bb_audio_close(&s->pb_dev);
        return -ENOMEM;
    }

    if (cfg->codec_ops && cfg->codec_ops->init) {
        int ret = cfg->codec_ops->init(&s->codec_ctx, cfg->rate, cfg->channels);
        if (ret < 0) {
            bb_log_error("stream: codec init failed");
            bb_pool_destroy(&s->mp_pool);
            bb_pool_destroy(&s->sp_pool);
            bb_audio_close(&s->cap_dev);
            bb_audio_close(&s->pb_dev);
            return ret;
        }
    }

    bb_log_info("stream init: %uHz %uch period=%u frames pool=%d",
                cfg->rate, cfg->channels, s->cap_dev.period_frames, nframes);
    return 0;
}

int bb_audio_stream_start(bb_audio_stream_t *s) {
    atomic_store(&s->running, true);
    atomic_store(&s->error, false);
    atomic_store(&s->cap_frames, 0);
    atomic_store(&s->pb_frames, 0);
    atomic_store(&s->drop_frames, 0);

    // Start in reverse dependency order: playback -> codec -> capture
    thread_ctx_t *pb_ctx = calloc(1, sizeof(thread_ctx_t));
    pb_ctx->s = s;
    s->pb_thread.entry        = playback_thread_fn;
    s->pb_thread.arg          = pb_ctx;
    s->pb_thread.cpu_affinity = s->cfg.cpu_playback;
    s->pb_thread.sched_policy = SCHED_FIFO;
    s->pb_thread.priority     = s->cfg.rt_priority;
    if (bb_thread_spawn(&s->pb_thread) < 0) {
        bb_log_error("stream: failed to spawn playback thread");
        free(pb_ctx);
        atomic_store(&s->running, false);
        return -1;
    }

    thread_ctx_t *cdc_ctx = calloc(1, sizeof(thread_ctx_t));
    cdc_ctx->s = s;
    s->codec_thread.entry        = codec_thread_fn;
    s->codec_thread.arg          = cdc_ctx;
    s->codec_thread.cpu_affinity = s->cfg.cpu_codec;
    s->codec_thread.sched_policy = SCHED_FIFO;
    s->codec_thread.priority     = s->cfg.rt_priority;
    if (bb_thread_spawn(&s->codec_thread) < 0) {
        bb_log_error("stream: failed to spawn codec thread");
        free(cdc_ctx);
        atomic_store(&s->running, false);
        bb_thread_join(&s->pb_thread);
        return -1;
    }

    thread_ctx_t *cap_ctx = calloc(1, sizeof(thread_ctx_t));
    cap_ctx->s = s;
    s->cap_thread.entry        = capture_thread_fn;
    s->cap_thread.arg          = cap_ctx;
    s->cap_thread.cpu_affinity = s->cfg.cpu_capture;
    s->cap_thread.sched_policy = SCHED_FIFO;
    s->cap_thread.priority     = s->cfg.rt_priority;
    if (bb_thread_spawn(&s->cap_thread) < 0) {
        bb_log_error("stream: failed to spawn capture thread");
        free(cap_ctx);
        atomic_store(&s->running, false);
        bb_thread_join(&s->codec_thread);
        bb_thread_join(&s->pb_thread);
        return -1;
    }

    bb_log_info("stream started (3 threads)");
    return 0;
}

int bb_audio_stream_stop(bb_audio_stream_t *s) {
    if (!atomic_load(&s->running)) return 0;

    bb_log_info("stream stopping...");
    atomic_store(&s->running, false);

    bb_thread_join(&s->cap_thread);
    bb_thread_join(&s->codec_thread);
    bb_thread_join(&s->pb_thread);

    ioctl(s->cap_dev.fd, SNDRV_PCM_IOCTL_DROP, 0);
    ioctl(s->pb_dev.fd, SNDRV_PCM_IOCTL_DROP, 0);

    bb_log_info("stream stopped: cap=%u pb=%u drop=%u",
                atomic_load(&s->cap_frames),
                atomic_load(&s->pb_frames),
                atomic_load(&s->drop_frames));
    return 0;
}

void bb_audio_stream_destroy(bb_audio_stream_t *s) {
    bb_audio_stream_stop(s);

    if (s->codec_ctx && s->cfg.codec_ops && s->cfg.codec_ops->destroy)
        s->cfg.codec_ops->destroy(s->codec_ctx);

    bb_pool_destroy(&s->sp_pool);
    bb_pool_destroy(&s->mp_pool);
    bb_audio_close(&s->cap_dev);
    bb_audio_close(&s->pb_dev);

    memset(s, 0, sizeof(*s));
}

void bb_audio_stream_stats(const bb_audio_stream_t *s,
                           uint32_t *cap, uint32_t *pb, uint32_t *drop) {
    if (cap)  *cap  = atomic_load(&s->cap_frames);
    if (pb)   *pb   = atomic_load(&s->pb_frames);
    if (drop) *drop = atomic_load(&s->drop_frames);
}
