/*
 * bb-audio-test - Audio HAL test for i.MX95
 *
 * Tests ALSA PCM capture and playback independently.
 * Usage: bb-audio-test [capture|playback] [duration_sec]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bb_hal_audio.h"

static void test_playback(int card, int dev, int duration_sec) {
    printf("=== Audio Playback Test ===\n");
    printf("Device: card=%d dev=%d, 48000Hz 2ch S16_LE, %d sec\n", card, dev, duration_sec);

    bb_audio_t a;
    if (bb_audio_open(&a, card, dev, BB_AUDIO_PLAYBACK, 48000, 2, BB_AUDIO_FMT_S16_LE) < 0) {
        fprintf(stderr, "Failed to open playback device\n");
        return;
    }

    // Generate 440Hz sine wave
    uint32_t period = a.period_frames;
    int16_t *buf = malloc(period * 2 * sizeof(int16_t));
    if (!buf) { bb_audio_close(&a); return; }

    uint32_t total_frames = 48000 * duration_sec;
    uint32_t written = 0;
    double phase = 0.0;
    const double freq = 440.0;
    const double sample_rate = 48000.0;

    while (written < total_frames) {
        for (uint32_t i = 0; i < period; i++) {
            double val = 0.3 * sin(2.0 * 3.1415926535 * freq * phase / sample_rate);
            int16_t sample = (int16_t)(val * 32767);
            buf[i * 2] = sample;
            buf[i * 2 + 1] = sample;
            phase += 1.0;
        }

        int n = bb_audio_write(&a, buf, period);
        if (n < 0) {
            fprintf(stderr, "Write error: %d\n", n);
            break;
        }
        written += n;
        if (written % 48000 == 0) {
            printf("  Playback: %u/%u frames\n", written, total_frames);
        }
    }

    free(buf);
    bb_audio_close(&a);
    printf("Playback test complete.\n");
}

static void test_capture(int card, int dev, int duration_sec) {
    printf("=== Audio Capture Test ===\n");
    printf("Device: card=%d dev=%d, 48000Hz 2ch S16_LE, %d sec\n", card, dev, duration_sec);

    bb_audio_t a;
    if (bb_audio_open(&a, card, dev, BB_AUDIO_CAPTURE, 48000, 2, BB_AUDIO_FMT_S16_LE) < 0) {
        fprintf(stderr, "Failed to open capture device\n");
        return;
    }

    uint32_t period = a.period_frames;
    int16_t *buf = malloc(period * 2 * sizeof(int16_t));
    if (!buf) { bb_audio_close(&a); return; }

    uint32_t total_frames = 48000 * duration_sec;
    uint32_t read_total = 0;
    double peak = 0.0;

    while (read_total < total_frames) {
        int n = bb_audio_read(&a, buf, period);
        if (n < 0) {
            fprintf(stderr, "Read error: %d\n", n);
            break;
        }
        // Track peak level
        for (int i = 0; i < n * 2; i++) {
            double v = abs(buf[i]) / 32768.0;
            if (v > peak) peak = v;
        }
        read_total += n;
        if (read_total % 48000 == 0) {
            printf("  Capture: %u/%u frames, peak=%.2f\n", read_total, total_frames, peak);
        }
    }

    free(buf);
    bb_audio_close(&a);
    printf("Capture test complete. Peak level: %.2f\n", peak);
}

int main(int argc, char *argv[]) {
    const char *mode = "playback";
    int duration = 3;
    int card = 0;
    int dev = 0;

    if (argc >= 2) mode = argv[1];
    if (argc >= 3) duration = atoi(argv[2]);

    printf("bb-audio-test for i.MX95\n");

    if (strcmp(mode, "capture") == 0) {
        test_capture(card, dev, duration);
    } else {
        test_playback(card, dev, duration);
    }

    return 0;
}
