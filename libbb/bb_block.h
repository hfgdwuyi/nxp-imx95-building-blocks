#ifndef BB_BLOCK_H
#define BB_BLOCK_H
#include "bb_types.h"
#include "bb_bus.h"

typedef struct bb_block {
    const char  *id;            // Block identifier, e.g. "bb-led"
    bb_bus_t    bus;            // Bus connection
    int         running;        // Main loop flag
    bb_state_t  state;          // Current state

    // Override in implementations
    int  (*init)(struct bb_block *block);
    int  (*run)(struct bb_block *block);      // Main loop iteration (return 0 to continue)
    void (*stop)(struct bb_block *block);
    void (*on_cmd)(struct bb_block *block, const char *topic, const char *payload);
} bb_block_t;

// Initialize block: connect to bus, subscribe to /dev/<id>/cmd, publish ready
int  bb_block_start(bb_block_t *block, const char *bus_path);

// Main event loop - blocks until stop signal received
void bb_block_main_loop(bb_block_t *block, int poll_ms);

// Publish block state to /dev/<id>/state
void bb_block_publish_state(bb_block_t *block, bb_state_t state, const char *extra_json);

// Publish event to /dev/<id>/event
void bb_block_publish_event(bb_block_t *block, const char *event_json);

// Signal block to stop
void bb_block_stop(bb_block_t *block);

#endif
