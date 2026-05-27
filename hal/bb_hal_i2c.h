#ifndef BB_HAL_I2C_H
#define BB_HAL_I2C_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int     fd;
    char    device[64];
    uint8_t addr;
} bb_i2c_t;

int  bb_i2c_open(bb_i2c_t *i2c, const char *device);
int  bb_i2c_set_addr(bb_i2c_t *i2c, uint8_t addr);
int  bb_i2c_write(bb_i2c_t *i2c, uint8_t addr, const uint8_t *data, size_t len);
int  bb_i2c_read(bb_i2c_t *i2c, uint8_t addr, uint8_t *buf, size_t len);
int  bb_i2c_write_read(bb_i2c_t *i2c, uint8_t addr,
                       const uint8_t *wbuf, size_t wlen,
                       uint8_t *rbuf, size_t rlen);
int  bb_i2c_probe(bb_i2c_t *i2c, uint8_t addr);
void bb_i2c_close(bb_i2c_t *i2c);

#endif
