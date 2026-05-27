#ifndef BB_HAL_DISPLAY_H
#define BB_HAL_DISPLAY_H
#include <stdint.h>

// DRM/KMS display HAL — raw ioctl, zero dependencies
// i.MX95 supports DSI, LVDS, and HDMI/DP via external bridges

typedef enum {
    BB_DISP_DSI  = 0,
    BB_DISP_LVDS = 1,
    BB_DISP_HDMI = 2,
    BB_DISP_DP   = 3,
    BB_DISP_ANY  = 4
} bb_disp_type_t;

typedef struct {
    int          fd;
    uint32_t     connector_id;
    uint32_t     crtc_id;
    uint32_t     encoder_id;
    uint32_t     plane_id;
    uint32_t     fb_id;
    uint32_t     width;
    uint32_t     height;
    uint32_t     stride;
    uint32_t     handle;
    uint64_t     size;
    void        *map;
    int          restored;
    uint32_t     saved_fb;
    uint32_t     saved_crtc;
    int          is_master;
} bb_display_t;

int  bb_display_open(bb_display_t *disp, const char *device, bb_disp_type_t type);
int  bb_display_get_fb(bb_display_t *disp, void **fb,
                       uint32_t *width, uint32_t *height, uint32_t *stride);
int  bb_display_flip(bb_display_t *disp);
void bb_display_close(bb_display_t *disp);

#endif
