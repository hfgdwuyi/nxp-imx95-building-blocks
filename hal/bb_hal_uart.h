#ifndef BB_HAL_UART_H
#define BB_HAL_UART_H
#include <stdint.h>
#include <stddef.h>

// i.MX95 uses ttyLP (LPUART) instead of ttymxc

typedef enum {
    BB_UART_BAUD_9600   = 9600,
    BB_UART_BAUD_19200  = 19200,
    BB_UART_BAUD_38400  = 38400,
    BB_UART_BAUD_57600  = 57600,
    BB_UART_BAUD_115200 = 115200,
    BB_UART_BAUD_230400 = 230400,
    BB_UART_BAUD_460800 = 460800,
    BB_UART_BAUD_921600 = 921600,
} bb_uart_baud_t;

typedef enum {
    BB_UART_PARITY_NONE = 0,
    BB_UART_PARITY_ODD  = 1,
    BB_UART_PARITY_EVEN = 2,
} bb_uart_parity_t;

typedef struct {
    int fd;
    char device[64];
} bb_uart_t;

int  bb_uart_open(bb_uart_t *uart, const char *device, bb_uart_baud_t baud);
int  bb_uart_set_baud(bb_uart_t *uart, bb_uart_baud_t baud);
int  bb_uart_set_parity(bb_uart_t *uart, bb_uart_parity_t parity);
int  bb_uart_write(bb_uart_t *uart, const uint8_t *data, size_t len);
int  bb_uart_read(bb_uart_t *uart, uint8_t *buf, size_t len, int timeout_ms);
int  bb_uart_available(bb_uart_t *uart);
void bb_uart_close(bb_uart_t *uart);

#endif
