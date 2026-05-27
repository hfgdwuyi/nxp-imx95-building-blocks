/*
 * bb-led - LED Controller Building Block
 *
 * Controls user LED via sysfs for i.MX95 EVK.
 * Subscribes: /dev/bb-led/cmd
 * Publishes:  /dev/bb-led/state, /dev/bb-led/event
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "bb_board.h"
#include "bb_block.h"
#include "bb_hal_led.h"
#include "bb_json.h"

static bb_led_t    g_led;
static pthread_t   g_blink_thread;
static int         g_blink_active = 0;
static int         g_blink_on_ms  = 500;
static int         g_blink_off_ms = 500;
static int         g_blink_count  = 0;
static bb_block_t *g_block;

static void *blink_thread(void *arg) {
    (void)arg;
    int cycles = 0;
    while (g_blink_active && (g_blink_count == 0 || cycles < g_blink_count)) {
        bb_led_on(&g_led);
        usleep(g_blink_on_ms * 1000);
        if (!g_blink_active) break;
        bb_led_off(&g_led);
        usleep(g_blink_off_ms * 1000);
        if (!g_blink_active) break;
        cycles++;
    }

    char evt[256];
    bb_json_writer_t w;
    bb_json_init(&w, evt, sizeof(evt));
    bb_json_start_object(&w);
    bb_json_add_string(&w, "type", "blink_done");
    bb_json_add_int(&w, "cycles", cycles);
    bb_json_end_object(&w);
    bb_block_publish_event(g_block, evt);
    return NULL;
}

static void on_cmd(bb_block_t *block, const char *topic, const char *payload) {
    (void)topic;
    const char *cmd = bb_json_get_string(payload, "cmd");
    if (!cmd) return;

    if (strcmp(cmd, "blink") == 0) {
        g_blink_active = 0;
        usleep(50000);

        g_blink_on_ms  = bb_json_get_int(payload, "on_ms", 500);
        g_blink_off_ms = bb_json_get_int(payload, "off_ms", 500);
        g_blink_count  = bb_json_get_int(payload, "count", 0);

        char evt[256];
        bb_json_writer_t w;
        bb_json_init(&w, evt, sizeof(evt));
        bb_json_start_object(&w);
        bb_json_add_string(&w, "type", "blink_start");
        bb_json_add_int(&w, "on_ms", g_blink_on_ms);
        bb_json_add_int(&w, "off_ms", g_blink_off_ms);
        bb_json_add_int(&w, "count", g_blink_count);
        bb_json_end_object(&w);
        bb_block_publish_event(block, evt);

        g_blink_active = 1;
        pthread_create(&g_blink_thread, NULL, blink_thread, NULL);
        pthread_detach(g_blink_thread);
    }
    else if (strcmp(cmd, "solid") == 0) {
        g_blink_active = 0;
        const char *state = bb_json_get_string(payload, "state");
        if (state && strcmp(state, "off") == 0) {
            bb_led_off(&g_led);
        } else {
            bb_led_on(&g_led);
        }
        char evt[256];
        bb_json_writer_t w;
        bb_json_init(&w, evt, sizeof(evt));
        bb_json_start_object(&w);
        bb_json_add_string(&w, "type", "solid");
        bb_json_add_string(&w, "state", state ? state : "on");
        bb_json_end_object(&w);
        bb_block_publish_event(block, evt);
    }
    else if (strcmp(cmd, "heartbeat") == 0) {
        g_blink_active = 0;
        bb_led_set_trigger(&g_led, "heartbeat");
        char evt[256];
        bb_json_writer_t w;
        bb_json_init(&w, evt, sizeof(evt));
        bb_json_start_object(&w);
        bb_json_add_string(&w, "type", "mode");
        bb_json_add_string(&w, "mode", "heartbeat");
        bb_json_end_object(&w);
        bb_block_publish_event(block, evt);
    }
    else if (strcmp(cmd, "status") == 0) {
        bb_block_publish_state(block, BB_STATE_RUNNING, NULL);
    }
    else if (strcmp(cmd, "stop") == 0) {
        g_blink_active = 0;
        bb_led_off(&g_led);
        char evt[128];
        bb_json_writer_t w;
        bb_json_init(&w, evt, sizeof(evt));
        bb_json_start_object(&w);
        bb_json_add_string(&w, "type", "stopped");
        bb_json_end_object(&w);
        bb_block_publish_event(block, evt);
    }
}

static int init(bb_block_t *block) {
    (void)block;
    if (bb_led_open(&g_led, BB_LED_HEARTBEAT) < 0) {
        fprintf(stderr, "[bb-led] Failed to open LED '%s'\n", BB_LED_HEARTBEAT);
        return -1;
    }
    bb_led_set_trigger(&g_led, "none");
    printf("[bb-led] LED '%s' opened, max_brightness=%d\n", g_led.name, g_led.max_brightness);
    return 0;
}

static void stop(bb_block_t *block) {
    (void)block;
    g_blink_active = 0;
    usleep(100000);
    bb_led_off(&g_led);
}

int main(void) {
    static bb_block_t block = {
        .id     = "bb-led",
        .init   = init,
        .stop   = stop,
        .on_cmd = on_cmd,
    };
    g_block = &block;

    if (bb_block_start(&block, BB_BUS_SOCKET) < 0) {
        return 1;
    }
    bb_block_main_loop(&block, 100);
    return 0;
}
