#include "bb_hal_spi.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

int bb_spi_open(bb_spi_t *spi, const char *device, uint32_t speed, uint8_t mode, uint8_t bits) {
    memset(spi, 0, sizeof(*spi));
    strncpy(spi->device, device, sizeof(spi->device) - 1);

    spi->fd = open(device, O_RDWR);
    if (spi->fd < 0) return -1;

    if (ioctl(spi->fd, SPI_IOC_WR_MODE32, &mode) < 0) {
        uint8_t m8 = mode;
        if (ioctl(spi->fd, SPI_IOC_WR_MODE, &m8) < 0) { close(spi->fd); return -1; }
    }

    if (bits != 8) {
        if (ioctl(spi->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
            close(spi->fd); return -1;
        }
    }

    if (ioctl(spi->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(spi->fd); return -1;
    }

    spi->speed_hz = speed;
    spi->mode = mode;
    spi->bits_per_word = bits;
    return 0;
}

int bb_spi_transfer(bb_spi_t *spi, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (spi->fd < 0) return -1;

    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = (uint32_t)len,
        .speed_hz      = spi->speed_hz,
        .bits_per_word = spi->bits_per_word,
    };

    return ioctl(spi->fd, SPI_IOC_MESSAGE(1), &tr) < 0 ? -1 : (int)len;
}

int bb_spi_write(bb_spi_t *spi, const uint8_t *data, size_t len) {
    uint8_t rx_buf[4096];
    size_t remaining = len;
    size_t offset = 0;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(rx_buf) ? sizeof(rx_buf) : remaining;
        if (bb_spi_transfer(spi, data + offset, rx_buf, chunk) < 0) return -1;
        offset += chunk;
        remaining -= chunk;
    }
    return (int)len;
}

void bb_spi_close(bb_spi_t *spi) {
    if (spi->fd >= 0) {
        close(spi->fd);
        spi->fd = -1;
    }
}
