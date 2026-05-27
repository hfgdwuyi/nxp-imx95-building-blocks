#include "bb_hal_rtc.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

int bb_rtc_open(bb_rtc_t *rtc, const char *device) {
    memset(rtc, 0, sizeof(*rtc));
    strncpy(rtc->device, device, sizeof(rtc->device) - 1);

    rtc->fd = open(device, O_RDWR);
    if (rtc->fd < 0) return -1;
    return 0;
}

int bb_rtc_read(bb_rtc_t *rtc, bb_rtc_time_t *t) {
    if (rtc->fd < 0) return -1;

    struct rtc_time rt;
    if (ioctl(rtc->fd, RTC_RD_TIME, &rt) < 0) return -1;

    t->year = rt.tm_year + 1900;
    t->mon  = rt.tm_mon + 1;
    t->day  = rt.tm_mday;
    t->hour = rt.tm_hour;
    t->min  = rt.tm_min;
    t->sec  = rt.tm_sec;
    return 0;
}

int bb_rtc_set(bb_rtc_t *rtc, const bb_rtc_time_t *t) {
    if (rtc->fd < 0) return -1;

    struct rtc_time rt;
    rt.tm_year = t->year - 1900;
    rt.tm_mon  = t->mon - 1;
    rt.tm_mday = t->day;
    rt.tm_hour = t->hour;
    rt.tm_min  = t->min;
    rt.tm_sec  = t->sec;

    return ioctl(rtc->fd, RTC_SET_TIME, &rt);
}

void bb_rtc_close(bb_rtc_t *rtc) {
    if (rtc->fd >= 0) {
        close(rtc->fd);
        rtc->fd = -1;
    }
}
