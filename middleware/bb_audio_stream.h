#ifndef BB_AUDIO_STREAM_H
#define BB_AUDIO_STREAM_H
/*
 * bb_audio_stream.h - Real-time audio stream manager
 *
 * Full-duplex audio pipeline on top of bb_hal_audio + bb_pool + bb_thread.
 * Supports PCM pass-through (codec_ops=NULL) or pluggable codec (Opus etc.).
 *
 * Thread architecture:
 *   capture_thread -> SPSC pool -> codec_thread -> MPSC pool -> playback_thread
 *
 * i.MX95: 6x A55 cores — recommend:
 *   cpu_capture=0, cpu_codec=1, cpu_playback=2
 */
#include "bb_hal_audio.h"
#include "bb_pool.h"
#include "bb_thread.h"
#include <stdatomic.h>

typedef struct bb_audio_codec_ops {
    int  (*init)(void **ctx, uint32_t rate, uint32_t channels);
    void (*destroy)(void *ctx);
    int  (*encode)(void *ctx, const uint8_t *pcm, size_t pcm_len,
                   uint8_t *out, size_t out_cap, size_t *out_len);
    int  (*decode)(void *ctx, const uint8_t *enc, size_t enc_len,
                   uint8_t *pcm, size_t pcm_cap, size_t *pcm_len);
} bb_audio_codec_ops_t;

typedef struct {
    int              card;
    int              dev;
    uint32_t         rate;
    uint32_t         channels;
    bb_audio_fmt_t   fmt;
    int              pool_frames;
    int              cpu_capture;
    int              cpu_codec;
    int              cpu_playback;
    int              rt_priority;
    bb_audio_codec_ops_t *codec_ops;
} bb_audio_stream_config_t;

#define BB_AUDIO_STREAM_CONFIG_DEFAULT { \
    .card = 0, .dev = 0, \
    .rate = 48000, .channels = 2, .fmt = BB_AUDIO_FMT_S16_LE, \
    .pool_frames = 4, \
    .cpu_capture = 0, .cpu_codec = 1, .cpu_playback = 2, \
    .rt_priority = 80, \
    .codec_ops = NULL, \
}

typedef struct {
    bb_audio_stream_config_t cfg;
    bb_audio_t        cap_dev;
    bb_audio_t        pb_dev;
    bb_frame_pool_t   sp_pool;
    bb_frame_pool_t   mp_pool;
    bb_thread_t       cap_thread;
    bb_thread_t       codec_thread;
    bb_thread_t       pb_thread;
    atomic_bool       running;
    atomic_bool       error;
    atomic_uint       cap_frames;
    atomic_uint       pb_frames;
    atomic_uint       drop_frames;
    void             *codec_ctx;
} bb_audio_stream_t;

int  bb_audio_stream_init(bb_audio_stream_t *s, const bb_audio_stream_config_t *cfg);
int  bb_audio_stream_start(bb_audio_stream_t *s);
int  bb_audio_stream_stop(bb_audio_stream_t *s);
void bb_audio_stream_destroy(bb_audio_stream_t *s);
void bb_audio_stream_stats(const bb_audio_stream_t *s,
                           uint32_t *cap, uint32_t *pb, uint32_t *drop);

#endif // BB_AUDIO_STREAM_H
