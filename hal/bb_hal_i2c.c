#include "bb_hal_i2c.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <errno.h>

int bb_i2c_open(bb_i2c_t *i2c, const char *device) {
    memset(i2c, 0, sizeof(*i2c));
    strncpy(i2c->device, device, sizeof(i2c->device) - 1);

    i2c->fd = open(device, O_RDWR);
    if (i2c->fd < 0) return -1;
    return 0;
}

int bb_i2c_set_addr(bb_i2c_t *i2c, uint8_t addr) {
    if (i2c->fd < 0) return -1;
    if (ioctl(i2c->fd, I2C_SLAVE, addr) < 0) return -1;
    i2c->addr = addr;
    return 0;
}

int bb_i2c_write(bb_i2c_t *i2c, uint8_t addr, const uint8_t *data, size_t len) {
    if (i2c->fd < 0) return -1;
    if (bb_i2c_set_addr(i2c, addr) < 0) return -1;
    return write(i2c->fd, data, len) == (ssize_t)len ? 0 : -1;
}

int bb_i2c_read(bb_i2c_t *i2c, uint8_t addr, uint8_t *buf, size_t len) {
    if (i2c->fd < 0) return -1;
    if (bb_i2c_set_addr(i2c, addr) < 0) return -1;
    return read(i2c->fd, buf, len) == (ssize_t)len ? 0 : -1;
}

int bb_i2c_write_read(bb_i2c_t *i2c, uint8_t addr,
                      const uint8_t *wbuf, size_t wlen,
                      uint8_t *rbuf, size_t rlen) {
    if (i2c->fd < 0) return -1;

    struct i2c_msg msgs[2] = {
        { .addr = addr, .flags = 0,          .len = wlen, .buf = (uint8_t *)wbuf },
        { .addr = addr, .flags = I2C_M_RD,   .len = rlen, .buf = rbuf },
    };

    struct i2c_rdwr_ioctl_data rdwr = {
        .msgs  = msgs,
        .nmsgs = (wlen > 0 && rlen > 0) ? 2 : (wlen > 0 ? 1 : (rlen > 0 ? 1 : 0)),
    };

    if (rdwr.nmsgs == 0) return 0;
    if (ioctl(i2c->fd, I2C_RDWR, &rdwr) < 0) return -1;
    return 0;
}

int bb_i2c_probe(bb_i2c_t *i2c, uint8_t addr) {
    if (i2c->fd < 0) return -1;
    if (ioctl(i2c->fd, I2C_SLAVE, addr) < 0) return -1;
    return write(i2c->fd, NULL, 0) == 0 ? 0 : -1;
}

void bb_i2c_close(bb_i2c_t *i2c) {
    if (i2c->fd >= 0) {
        close(i2c->fd);
        i2c->fd = -1;
    }
}
