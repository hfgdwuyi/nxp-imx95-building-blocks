#ifndef BB_TYPES_H
#define BB_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define BB_BUS_SOCKET    "/run/bb-bus.sock"
#define BB_MAX_TOPIC     128
#define BB_MAX_PAYLOAD   4096
#define BB_MAX_LINE      (BB_MAX_TOPIC + BB_MAX_PAYLOAD + 64)

// Block lifecycle states
typedef enum {
    BB_STATE_INIT,
    BB_STATE_READY,
    BB_STATE_RUNNING,
    BB_STATE_STOPPING,
    BB_STATE_STOPPED,
    BB_STATE_ERROR
} bb_state_t;

// Message handler callback
typedef void (*bb_msg_handler_t)(const char *topic, const char *payload, void *userdata);

#endif
