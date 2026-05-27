#include "bb_hal_wdg.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>

int bb_wdg_open(bb_wdg_t *wdg, const char *device) {
    memset(wdg, 0, sizeof(*wdg));

    wdg->fd = open(device, O_RDWR);
    if (wdg->fd < 0) return -1;

    if (ioctl(wdg->fd, WDIOC_GETTIMEOUT, &wdg->timeout) < 0) {
        wdg->timeout = 60;
    }
    return 0;
}

int bb_wdg_set_timeout(bb_wdg_t *wdg, int seconds) {
    if (wdg->fd < 0) return -1;

    if (ioctl(wdg->fd, WDIOC_SETTIMEOUT, &seconds) < 0) return -1;
    wdg->timeout = seconds;
    return 0;
}

int bb_wdg_get_timeout(bb_wdg_t *wdg) {
    if (wdg->fd < 0) return -1;

    int timeout = 0;
    if (ioctl(wdg->fd, WDIOC_GETTIMEOUT, &timeout) < 0) return -1;
    wdg->timeout = timeout;
    return timeout;
}

int bb_wdg_kick(bb_wdg_t *wdg) {
    if (wdg->fd < 0) return -1;
    return write(wdg->fd, "k", 1) > 0 ? 0 : -1;
}

void bb_wdg_close(bb_wdg_t *wdg) {
    if (wdg->fd >= 0) {
        write(wdg->fd, "V", 1);
        close(wdg->fd);
        wdg->fd = -1;
    }
}
