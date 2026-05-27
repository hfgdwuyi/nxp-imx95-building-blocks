#ifndef BB_HAL_AUDIO_H
#define BB_HAL_AUDIO_H
#include <stdint.h>

// ALSA PCM HAL — raw ioctl, zero dependencies
// i.MX95 audio: WM8960/WM8962 or similar codec via SAI/I2S

typedef enum {
    BB_AUDIO_CAPTURE  = 0,
    BB_AUDIO_PLAYBACK = 1,
} bb_audio_dir_t;

typedef enum {
    BB_AUDIO_FMT_S16_LE = 0,
    BB_AUDIO_FMT_S24_LE = 1,
    BB_AUDIO_FMT_S32_LE = 2,
} bb_audio_fmt_t;

typedef struct {
    int              fd;
    bb_audio_dir_t   dir;
    uint32_t         rate;
    uint32_t         channels;
    bb_audio_fmt_t   fmt;
    uint32_t         period_frames;
    uint32_t         period_bytes;
    uint32_t         buffer_frames;
} bb_audio_t;

int  bb_audio_open(bb_audio_t *a, int card, int dev, bb_audio_dir_t dir,
                   uint32_t rate, uint32_t channels, bb_audio_fmt_t fmt);
int  bb_audio_read(bb_audio_t *a, void *buf, uint32_t frames);
int  bb_audio_write(bb_audio_t *a, const void *buf, uint32_t frames);
void bb_audio_close(bb_audio_t *a);

#endif
