#ifndef BB_HAL_WDG_H
#define BB_HAL_WDG_H

typedef struct {
    int fd;
    int timeout;
} bb_wdg_t;

int  bb_wdg_open(bb_wdg_t *wdg, const char *device);
int  bb_wdg_set_timeout(bb_wdg_t *wdg, int seconds);
int  bb_wdg_get_timeout(bb_wdg_t *wdg);
int  bb_wdg_kick(bb_wdg_t *wdg);
void bb_wdg_close(bb_wdg_t *wdg);

#endif
