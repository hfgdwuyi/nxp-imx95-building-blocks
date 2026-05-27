#include "bb_hal_uart.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <errno.h>

int bb_uart_open(bb_uart_t *uart, const char *device, bb_uart_baud_t baud) {
    memset(uart, 0, sizeof(*uart));
    strncpy(uart->device, device, sizeof(uart->device) - 1);

    uart->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (uart->fd < 0) return -1;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(uart->fd, &tty) < 0) { close(uart->fd); return -1; }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(uart->fd, TCSANOW, &tty) < 0) { close(uart->fd); return -1; }

    return bb_uart_set_baud(uart, baud);
}

int bb_uart_set_baud(bb_uart_t *uart, bb_uart_baud_t baud) {
    if (uart->fd < 0) return -1;

    struct termios tty;
    if (tcgetattr(uart->fd, &tty) < 0) return -1;

    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:     return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    return tcsetattr(uart->fd, TCSANOW, &tty);
}

int bb_uart_set_parity(bb_uart_t *uart, bb_uart_parity_t parity) {
    if (uart->fd < 0) return -1;

    struct termios tty;
    if (tcgetattr(uart->fd, &tty) < 0) return -1;

    switch (parity) {
        case BB_UART_PARITY_NONE: tty.c_cflag &= ~PARENB; break;
        case BB_UART_PARITY_ODD:  tty.c_cflag |= PARENB | PARODD; break;
        case BB_UART_PARITY_EVEN: tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; break;
    }

    return tcsetattr(uart->fd, TCSANOW, &tty);
}

int bb_uart_write(bb_uart_t *uart, const uint8_t *data, size_t len) {
    if (uart->fd < 0) return -1;
    return write(uart->fd, data, len) == (ssize_t)len ? 0 : -1;
}

int bb_uart_read(bb_uart_t *uart, uint8_t *buf, size_t len, int timeout_ms) {
    if (uart->fd < 0) return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(uart->fd, &fds);

    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    if (select(uart->fd + 1, &fds, NULL, NULL, &tv) <= 0) return 0;

    int n = read(uart->fd, buf, len);
    if (n < 0 && errno == EAGAIN) return 0;
    return n;
}

int bb_uart_available(bb_uart_t *uart) {
    if (uart->fd < 0) return -1;
    int n = 0;
    ioctl(uart->fd, FIONREAD, &n);
    return n;
}

void bb_uart_close(bb_uart_t *uart) {
    if (uart->fd >= 0) {
        close(uart->fd);
        uart->fd = -1;
    }
}
