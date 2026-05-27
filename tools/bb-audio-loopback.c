/*
 * bb-audio-loopback - Full-duplex audio loopback test for i.MX95
 *
 * Captures from mic and plays back to speaker simultaneously.
 * Usage: bb-audio-loopback [duration_sec]
 *
 * Default card=0 (WM8960 codec on i.MX95 EVK).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bb_audio_stream.h"
#include "bb_log.h"

int main(int argc, char *argv[]) {
    int duration = 5;
    if (argc >= 2) duration = atoi(argv[1]);

    printf("bb-audio-loopback for i.MX95\n");
    printf("Full-duplex PCM loopback, %d seconds\n", duration);
    printf("(speak into mic, hear yourself)\n");

    bb_log_init("audio-loopback", BB_LOG_TO_STDERR, NULL, 0);
    bb_log_set_level(BB_LOG_INFO);

    bb_audio_stream_config_t cfg = BB_AUDIO_STREAM_CONFIG_DEFAULT;
    // Override card to 0 for i.MX95 WM8960 codec
    cfg.card = 0;

    bb_audio_stream_t stream;
    if (bb_audio_stream_init(&stream, &cfg) < 0) {
        fprintf(stderr, "Failed to initialize audio stream\n"
                        "Check: is the WM8960 codec loaded? Try 'aplay -l'\n");
        return 1;
    }

    if (bb_audio_stream_start(&stream) < 0) {
        fprintf(stderr, "Failed to start audio stream\n");
        bb_audio_stream_destroy(&stream);
        return 1;
    }

    printf("Stream running for %d seconds...\n", duration);
    sleep(duration);

    bb_audio_stream_stop(&stream);

    uint32_t cap, pb, drop;
    bb_audio_stream_stats(&stream, &cap, &pb, &drop);
    printf("Stats: captured=%u frames, played=%u frames, dropped=%u frames\n",
           cap, pb, drop);

    bb_audio_stream_destroy(&stream);
    printf("Done.\n");
    return 0;
}
