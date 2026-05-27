#ifndef BB_BUS_H
#define BB_BUS_H
#include "bb_types.h"

typedef struct {
    int fd;
    char socket_path[128];
    bb_msg_handler_t handler;
    void *userdata;
} bb_bus_t;

// Connect to local bus daemon
int bb_bus_connect(bb_bus_t *bus, const char *socket_path);

// Publish a message to a topic
int bb_bus_publish(bb_bus_t *bus, const char *topic, const char *payload);

// Subscribe to a topic pattern
int bb_bus_subscribe(bb_bus_t *bus, const char *topic);

// Unsubscribe from a topic
int bb_bus_unsubscribe(bb_bus_t *bus, const char *topic);

// Send PING, expect PONG
int bb_bus_ping(bb_bus_t *bus);

// Set message handler for incoming PUB messages
void bb_bus_set_handler(bb_bus_t *bus, bb_msg_handler_t handler, void *userdata);

// Read and dispatch incoming messages (non-blocking)
int bb_bus_poll(bb_bus_t *bus, int timeout_ms);

// Close connection
void bb_bus_close(bb_bus_t *bus);

#endif
