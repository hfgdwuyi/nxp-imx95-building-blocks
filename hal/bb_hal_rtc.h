#ifndef BB_HAL_RTC_H
#define BB_HAL_RTC_H
#include <stdint.h>

typedef struct {
    int fd;
    char device[64];
} bb_rtc_t;

typedef struct {
    int year;
    int mon;
    int day;
    int hour;
    int min;
    int sec;
} bb_rtc_time_t;

int  bb_rtc_open(bb_rtc_t *rtc, const char *device);
int  bb_rtc_read(bb_rtc_t *rtc, bb_rtc_time_t *t);
int  bb_rtc_set(bb_rtc_t *rtc, const bb_rtc_time_t *t);
void bb_rtc_close(bb_rtc_t *rtc);

#endif
