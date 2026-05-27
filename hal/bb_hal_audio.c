/*
 * bb_hal_audio.c - ALSA PCM HAL (raw ioctl, zero dependencies)
 *
 * i.MX95 audio uses SAI (Synchronous Audio Interface) with
 * external codec (WM8960/WM8962 etc.) via I2S.
 */
#include "bb_hal_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

static void snd_mask_any(struct snd_mask *m) {
    for (int i = 0; i < 8; i++) m->bits[i] = ~0U;
}

static void snd_mask_set(struct snd_mask *m, unsigned int val) {
    if (val < 256) m->bits[val / 32] |= 1U << (val % 32);
}

static void snd_interval_any(struct snd_interval *i) {
    i->min = 0;
    i->max = ~0U;
    i->openmin = 0;
    i->openmax = 0;
    i->integer = 1;
    i->empty = 0;
}

static void snd_interval_set(struct snd_interval *i, unsigned int val) {
    i->min = val;
    i->max = val;
    i->openmin = 0;
    i->openmax = 0;
    i->integer = 1;
    i->empty = 0;
}

static unsigned int clamp_u32(unsigned int v, unsigned int lo, unsigned int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static const snd_pcm_format_t fmt_map[] = {
    [BB_AUDIO_FMT_S16_LE] = SNDRV_PCM_FORMAT_S16_LE,
    [BB_AUDIO_FMT_S24_LE] = SNDRV_PCM_FORMAT_S24_LE,
    [BB_AUDIO_FMT_S32_LE] = SNDRV_PCM_FORMAT_S32_LE,
};

static const char *fmt_str(bb_audio_fmt_t f) {
    switch (f) {
    case BB_AUDIO_FMT_S16_LE: return "S16_LE";
    case BB_AUDIO_FMT_S24_LE: return "S24_LE";
    case BB_AUDIO_FMT_S32_LE: return "S32_LE";
    default: return "?";
    }
}

static int fmt_bytes(bb_audio_fmt_t f) {
    switch (f) {
    case BB_AUDIO_FMT_S16_LE: return 2;
    case BB_AUDIO_FMT_S24_LE: return 3;
    case BB_AUDIO_FMT_S32_LE: return 4;
    default: return 2;
    }
}

int bb_audio_open(bb_audio_t *a, int card, int dev, bb_audio_dir_t dir,
                  uint32_t rate, uint32_t channels, bb_audio_fmt_t fmt) {
    memset(a, 0, sizeof(*a));
    a->fd = -1;
    a->dir = dir;
    a->fmt = fmt;

    char path[64];
    snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%d%c",
             card, dev, dir == BB_AUDIO_CAPTURE ? 'c' : 'p');

    a->fd = open(path, O_RDWR);
    if (a->fd < 0) {
        perror("bb_audio_open");
        return -1;
    }

    // Phase 1: HW_REFINE to discover hardware constraints
    struct snd_pcm_hw_params hw = {0};

    for (int i = 0; i < 3; i++) snd_mask_any(&hw.masks[i]);
    for (int i = 0; i < 12; i++) snd_interval_any(&hw.intervals[i]);
    hw.rmask = ~0U;

    if (ioctl(a->fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) {
        perror("SNDRV_PCM_IOCTL_HW_REFINE");
        goto fail;
    }

    // Phase 2: Narrow to requested parameters
    memset(&hw.masks[0], 0, sizeof(hw.masks[0]));
    snd_mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK],
                 SNDRV_PCM_ACCESS_RW_INTERLEAVED);

    memset(&hw.masks[1], 0, sizeof(hw.masks[1]));
    snd_mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK],
                 fmt_map[fmt]);

    memset(&hw.masks[2], 0, sizeof(hw.masks[2]));
    snd_mask_set(&hw.masks[SNDRV_PCM_HW_PARAM_SUBFORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK],
                 SNDRV_PCM_SUBFORMAT_STD);

    unsigned int ch_min = hw.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    unsigned int ch_max = hw.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
    channels = clamp_u32(channels, ch_min, ch_max);
    snd_interval_set(&hw.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL],
                     channels);

    unsigned int rate_min = hw.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    unsigned int rate_max = hw.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
    rate = clamp_u32(rate, rate_min, rate_max);
    snd_interval_set(&hw.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL],
                     rate);

    unsigned int ps_min = hw.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    unsigned int ps_max = hw.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
    uint32_t period_target = clamp_u32(rate / 100, ps_min, ps_max);
    if (period_target < 16) period_target = 16;
    if (period_target > ps_max) period_target = ps_max;
    snd_interval_set(&hw.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL],
                     period_target);

    unsigned int bs_max = hw.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
    unsigned int bb_max = hw.intervals[SNDRV_PCM_HW_PARAM_BUFFER_BYTES - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max;
    unsigned int bs_from_bytes = bb_max / (channels * fmt_bytes(fmt));
    if (bs_max > bs_from_bytes) bs_max = bs_from_bytes;
    uint32_t buffer_target = (bs_max / period_target) * period_target;
    snd_interval_set(&hw.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL],
                     buffer_target);

    hw.rmask = (1U << SNDRV_PCM_HW_PARAM_ACCESS) |
               (1U << SNDRV_PCM_HW_PARAM_FORMAT) |
               (1U << SNDRV_PCM_HW_PARAM_SUBFORMAT) |
               (1U << SNDRV_PCM_HW_PARAM_CHANNELS) |
               (1U << SNDRV_PCM_HW_PARAM_RATE) |
               (1U << SNDRV_PCM_HW_PARAM_PERIOD_SIZE) |
               (1U << SNDRV_PCM_HW_PARAM_BUFFER_SIZE);
    hw.flags = SNDRV_PCM_HW_PARAMS_NORESAMPLE;

    if (ioctl(a->fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0) {
        perror("SNDRV_PCM_IOCTL_HW_PARAMS");
        goto fail;
    }

    a->rate     = hw.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    a->channels = hw.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    a->period_frames = hw.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    a->buffer_frames = hw.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    a->period_bytes  = a->period_frames * a->channels * fmt_bytes(a->fmt);

    // Phase 3: Set SW params
    struct snd_pcm_sw_params sw = {0};
    sw.tstamp_mode      = SNDRV_PCM_TSTAMP_ENABLE;
    sw.period_step      = 1;
    sw.avail_min        = a->period_frames;
    sw.start_threshold  = (dir == BB_AUDIO_CAPTURE) ? 1 : a->period_frames * 2;
    sw.stop_threshold   = a->buffer_frames;
    sw.silence_threshold = 0;
    sw.silence_size     = 0;
    sw.boundary         = a->buffer_frames * 4;
    while (sw.boundary < a->buffer_frames * 2)
        sw.boundary *= 2;

    if (ioctl(a->fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0) {
        perror("SNDRV_PCM_IOCTL_SW_PARAMS");
        goto fail;
    }

    // Phase 4: Prepare
    if (ioctl(a->fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
        perror("SNDRV_PCM_IOCTL_PREPARE");
        goto fail;
    }

    printf("[audio] %s opened on %s: %uHz %uch %s period=%u buf=%u\n",
           dir == BB_AUDIO_CAPTURE ? "capture" : "playback",
           path, a->rate, a->channels, fmt_str(fmt),
           a->period_frames, a->buffer_frames);
    return 0;

fail:
    close(a->fd);
    a->fd = -1;
    return -1;
}

int bb_audio_read(bb_audio_t *a, void *buf, uint32_t frames) {
    if (a->fd < 0 || a->dir != BB_AUDIO_CAPTURE) return -EINVAL;
    ssize_t bytes = frames * a->channels * fmt_bytes(a->fmt);
    ssize_t n = read(a->fd, buf, bytes);
    if (n < 0) return -errno;
    return n / (a->channels * fmt_bytes(a->fmt));
}

int bb_audio_write(bb_audio_t *a, const void *buf, uint32_t frames) {
    if (a->fd < 0 || a->dir != BB_AUDIO_PLAYBACK) return -EINVAL;
    ssize_t bytes = frames * a->channels * fmt_bytes(a->fmt);
    ssize_t n = write(a->fd, buf, bytes);
    if (n < 0) return -errno;
    return n / (a->channels * fmt_bytes(a->fmt));
}

void bb_audio_close(bb_audio_t *a) {
    if (a->fd < 0) return;
    ioctl(a->fd, SNDRV_PCM_IOCTL_DROP, 0);
    close(a->fd);
    a->fd = -1;
}
