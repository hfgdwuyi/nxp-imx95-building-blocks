/*
 * bb-display-test - DRM/KMS display test for i.MX95
 *
 * Draws color bars to test display output.
 * Usage: bb-display-test [dsi|lvds|hdmi|dp]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bb_hal_display.h"

static uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
}

static void draw_color_bars(uint32_t *fb, uint32_t w, uint32_t h, uint32_t stride_pixels) {
    const uint32_t colors[] = {
        make_color(255, 0, 0),     // Red
        make_color(0, 255, 0),     // Green
        make_color(0, 0, 255),     // Blue
        make_color(255, 255, 0),   // Yellow
        make_color(0, 255, 255),   // Cyan
        make_color(255, 0, 255),   // Magenta
        make_color(255, 255, 255), // White
        make_color(0, 0, 0),       // Black
    };
    int n_colors = sizeof(colors) / sizeof(colors[0]);
    uint32_t bar_width = w / n_colors;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int bar = x / bar_width;
            if (bar >= n_colors) bar = n_colors - 1;
            fb[y * stride_pixels + x] = colors[bar];
        }
    }
}

int main(int argc, char *argv[]) {
    bb_disp_type_t type = BB_DISP_ANY;

    if (argc >= 2) {
        if (strcmp(argv[1], "dsi") == 0)      type = BB_DISP_DSI;
        else if (strcmp(argv[1], "lvds") == 0) type = BB_DISP_LVDS;
        else if (strcmp(argv[1], "hdmi") == 0) type = BB_DISP_HDMI;
        else if (strcmp(argv[1], "dp") == 0)   type = BB_DISP_DP;
    }

    printf("bb-display-test for i.MX95\n");

    bb_display_t disp;
    if (bb_display_open(&disp, "/dev/dri/card0", type) < 0) {
        fprintf(stderr, "Failed to open display. Try /dev/dri/card1?\n");
        // Try card1 as fallback
        if (bb_display_open(&disp, "/dev/dri/card1", type) < 0) {
            return 1;
        }
    }

    void *fb;
    uint32_t width, height, stride;
    if (bb_display_get_fb(&disp, &fb, &width, &height, &stride) < 0) {
        fprintf(stderr, "Failed to get framebuffer\n");
        bb_display_close(&disp);
        return 1;
    }

    printf("Display: %ux%u, stride=%u\n", width, height, stride);
    printf("Drawing color bars for 5 seconds...\n");

    draw_color_bars((uint32_t *)fb, width, height, stride / 4);
    bb_display_flip(&disp);

    sleep(5);

    bb_display_close(&disp);
    printf("Done.\n");
    return 0;
}
