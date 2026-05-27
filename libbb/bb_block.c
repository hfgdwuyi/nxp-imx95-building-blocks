#include "bb_block.h"
#include "bb_json.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

static bb_block_t *g_current_block = NULL;

static void sig_handler(int sig) {
    (void)sig;
    if (g_current_block) {
        g_current_block->running = 0;
    }
}

static void cmd_dispatcher(const char *topic, const char *payload, void *userdata) {
    bb_block_t *block = (bb_block_t *)userdata;
    if (block && block->on_cmd) {
        block->on_cmd(block, topic, payload);
    }
}

int bb_block_start(bb_block_t *block, const char *bus_path) {
    g_current_block = block;
    block->running = 1;
    block->state = BB_STATE_INIT;

    if (bb_bus_connect(&block->bus, bus_path) < 0) {
        fprintf(stderr, "[%s] Failed to connect to bus at %s\n", block->id, bus_path);
        return -1;
    }

    bb_bus_set_handler(&block->bus, cmd_dispatcher, block);

    // Subscribe to cmd and broadcast topics
    char topic[BB_MAX_TOPIC];
    snprintf(topic, sizeof(topic), "/dev/%s/cmd", block->id);
    bb_bus_subscribe(&block->bus, topic);
    bb_bus_subscribe(&block->bus, "/system/broadcast");

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    if (block->init) {
        block->init(block);
    }

    bb_block_publish_state(block, BB_STATE_READY, NULL);
    return 0;
}

void bb_block_main_loop(bb_block_t *block, int poll_ms) {
    block->state = BB_STATE_RUNNING;
    bb_block_publish_state(block, BB_STATE_RUNNING, NULL);

    while (block->running) {
        bb_bus_poll(&block->bus, poll_ms);
        if (block->run) {
            if (block->run(block) != 0) break;
        }
    }

    block->state = BB_STATE_STOPPING;
    if (block->stop) {
        block->stop(block);
    }
    bb_block_publish_state(block, BB_STATE_STOPPED, NULL);
    bb_bus_close(&block->bus);
}

void bb_block_publish_state(bb_block_t *block, bb_state_t state, const char *extra_json) {
    (void)extra_json;
    block->state = state;
    char payload[BB_MAX_PAYLOAD];
    char topic[BB_MAX_TOPIC];

    bb_json_writer_t w;
    bb_json_init(&w, payload, sizeof(payload));
    bb_json_start_object(&w);
    bb_json_add_string(&w, "block", block->id);
    bb_json_add_int(&w, "ts", (int)time(NULL));

    const char *state_str = "unknown";
    switch (state) {
        case BB_STATE_INIT:     state_str = "init"; break;
        case BB_STATE_READY:    state_str = "ready"; break;
        case BB_STATE_RUNNING:  state_str = "running"; break;
        case BB_STATE_STOPPING: state_str = "stopping"; break;
        case BB_STATE_STOPPED:  state_str = "stopped"; break;
        case BB_STATE_ERROR:    state_str = "error"; break;
    }
    bb_json_add_string(&w, "status", state_str);
    bb_json_end_object(&w);

    snprintf(topic, sizeof(topic), "/dev/%s/state", block->id);
    bb_bus_publish(&block->bus, topic, payload);
}

void bb_block_publish_event(bb_block_t *block, const char *event_json) {
    char topic[BB_MAX_TOPIC];
    snprintf(topic, sizeof(topic), "/dev/%s/event", block->id);
    bb_bus_publish(&block->bus, topic, event_json);
}

void bb_block_stop(bb_block_t *block) {
    block->running = 0;
}
