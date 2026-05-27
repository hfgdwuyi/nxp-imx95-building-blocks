/*
 * bb_hal_display.c - DRM/KMS display HAL (raw ioctl, zero dependencies)
 *
 * i.MX95 display: DCSS (Display Controller Sub-System) with
 * DSI, LVDS, and HDMI/DP output support.
 */
#include "bb_hal_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED        1
#endif
#ifndef DRM_MODE_DISCONNECTED
#define DRM_MODE_DISCONNECTED     2
#endif

#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888       fourcc_code('X', 'R', '2', '4')
#endif
#ifndef fourcc_code
#define fourcc_code(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#endif

static const char *type_name(bb_disp_type_t t) {
    switch (t) {
        case BB_DISP_DSI:  return "DSI";
        case BB_DISP_LVDS: return "LVDS";
        case BB_DISP_HDMI: return "HDMI";
        case BB_DISP_DP:   return "DP";
        default:           return "ANY";
    }
}

int bb_display_open(bb_display_t *disp, const char *device, bb_disp_type_t type) {
    memset(disp, 0, sizeof(*disp));
    disp->fd = -1;

    disp->fd = open(device, O_RDWR);
    if (disp->fd < 0) { perror("drm open"); return -1; }

    ioctl(disp->fd, DRM_IOCTL_SET_MASTER, 0);
    disp->is_master = 1;

    struct drm_mode_card_res res = {0};
    if (ioctl(disp->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES"); goto fail;
    }

    uint32_t conn_ids[32], enc_ids[32], crtc_ids[32];
    memset(conn_ids, 0, sizeof(conn_ids));
    memset(enc_ids, 0, sizeof(enc_ids));
    memset(crtc_ids, 0, sizeof(crtc_ids));

    struct drm_mode_card_res res2 = {0};
    res2.count_connectors = res.count_connectors > 32 ? 32 : res.count_connectors;
    res2.count_encoders   = res.count_encoders   > 32 ? 32 : res.count_encoders;
    res2.count_crtcs      = res.count_crtcs      > 32 ? 32 : res.count_crtcs;
    res2.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
    res2.encoder_id_ptr   = (uint64_t)(uintptr_t)enc_ids;
    res2.crtc_id_ptr      = (uint64_t)(uintptr_t)crtc_ids;

    if (ioctl(disp->fd, DRM_IOCTL_MODE_GETRESOURCES, &res2) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES2"); goto fail;
    }

    res = res2;

    int found = 0;
    struct drm_mode_modeinfo best_mode = {0};

    for (int i = 0; i < (int)res.count_connectors && !found; i++) {
        struct drm_mode_get_connector conn = {0};
        conn.connector_id = conn_ids[i];

        if (ioctl(disp->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0)
            continue;
        if (conn.connection != DRM_MODE_CONNECTED)
            continue;

        int match = 0;
        if (type == BB_DISP_ANY) {
            match = 1;
        } else if (type == BB_DISP_DSI  && conn.connector_type == DRM_MODE_CONNECTOR_DSI) {
            match = 1;
        } else if (type == BB_DISP_LVDS && conn.connector_type == DRM_MODE_CONNECTOR_LVDS) {
            match = 1;
        } else if (type == BB_DISP_HDMI &&
                   (conn.connector_type == DRM_MODE_CONNECTOR_HDMIA ||
                    conn.connector_type == DRM_MODE_CONNECTOR_HDMIB)) {
            match = 1;
        } else if (type == BB_DISP_DP &&
                   conn.connector_type == DRM_MODE_CONNECTOR_DisplayPort) {
            match = 1;
        }
        if (!match) continue;

        struct drm_mode_modeinfo modes[4];
        memset(modes, 0, sizeof(modes));
        struct drm_mode_get_connector conn2 = {0};
        conn2.connector_id = conn.connector_id;
        conn2.modes_ptr    = (uint64_t)(uintptr_t)modes;
        conn2.count_modes  = conn.count_modes > 4 ? 4 : conn.count_modes;
        if (ioctl(disp->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn2) < 0)
            continue;

        memcpy(&best_mode, &modes[0], sizeof(best_mode));

        disp->encoder_id = conn.encoder_id;
        struct drm_mode_get_encoder enc = {0};
        enc.encoder_id = conn.encoder_id;
        if (ioctl(disp->fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0)
            disp->crtc_id = enc.crtc_id;

        disp->connector_id = conn.connector_id;
        disp->width  = best_mode.hdisplay;
        disp->height = best_mode.vdisplay;
        found = 1;
    }

    if (!found) {
        fprintf(stderr, "bb_display_open: no %s connector found\n", type_name(type));
        goto fail;
    }

    // Save current CRTC state for restore
    struct drm_mode_crtc saved_crtc = {0};
    saved_crtc.crtc_id = disp->crtc_id;
    if (ioctl(disp->fd, DRM_IOCTL_MODE_GETCRTC, &saved_crtc) == 0) {
        disp->saved_fb = saved_crtc.fb_id;
        disp->restored = 1;
    }

    // Create dumb buffer
    struct drm_mode_create_dumb create = {0};
    create.width  = disp->width;
    create.height = disp->height;
    create.bpp    = 32;
    if (ioctl(disp->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB"); goto fail;
    }
    disp->handle = create.handle;
    disp->stride = create.pitch;
    disp->size   = create.size;

    // Add framebuffer
    struct drm_mode_fb_cmd2 fb_cmd = {0};
    fb_cmd.width     = disp->width;
    fb_cmd.height    = disp->height;
    fb_cmd.pixel_format = DRM_FORMAT_XRGB8888;
    fb_cmd.handles[0] = disp->handle;
    fb_cmd.pitches[0] = disp->stride;
    fb_cmd.offsets[0] = 0;
    if (ioctl(disp->fd, DRM_IOCTL_MODE_ADDFB2, &fb_cmd) < 0) {
        perror("DRM_IOCTL_MODE_ADDFB2"); goto fail_dumb;
    }
    disp->fb_id = fb_cmd.fb_id;

    // mmap buffer
    struct drm_mode_map_dumb map = {0};
    map.handle = disp->handle;
    if (ioctl(disp->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB"); goto fail_fb;
    }
    disp->map = mmap(0, disp->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     disp->fd, map.offset);
    if (disp->map == MAP_FAILED) {
        perror("mmap"); goto fail_fb;
    }

    // Initial modeset
    {
        uint32_t conn = disp->connector_id;
        struct drm_mode_crtc crtc = {0};
        crtc.crtc_id     = disp->crtc_id;
        crtc.fb_id       = disp->fb_id;
        crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&conn;
        crtc.count_connectors   = 1;
        crtc.mode_valid  = 1;
        crtc.x           = 0;
        crtc.y           = 0;
        memcpy(&crtc.mode, &best_mode, sizeof(best_mode));

        if (ioctl(disp->fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
            perror("DRM_IOCTL_MODE_SETCRTC"); goto fail_mmap;
        }
    }

    printf("[display] %s %dx%d opened on %s\n",
           type_name(type), disp->width, disp->height, device);
    return 0;

fail_mmap:
    munmap(disp->map, disp->size);
    disp->map = NULL;
fail_fb:
    { struct drm_mode_fb_cmd2 tmp = { .fb_id = disp->fb_id };
      ioctl(disp->fd, DRM_IOCTL_MODE_RMFB, &tmp); }
    disp->fb_id = 0;
fail_dumb:
    { struct drm_mode_destroy_dumb tmp = { .handle = disp->handle };
      ioctl(disp->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &tmp); }
    disp->handle = 0;
fail:
    close(disp->fd);
    disp->fd = -1;
    return -1;
}

int bb_display_get_fb(bb_display_t *disp, void **fb,
                       uint32_t *width, uint32_t *height, uint32_t *stride) {
    if (disp->fd < 0 || !disp->map) return -1;
    *fb     = disp->map;
    *width  = disp->width;
    *height = disp->height;
    *stride = disp->stride;
    return 0;
}

int bb_display_flip(bb_display_t *disp) {
    if (disp->fd < 0) return -1;

    struct drm_mode_crtc_page_flip flip = {0};
    flip.crtc_id = disp->crtc_id;
    flip.fb_id   = disp->fb_id;
    flip.flags   = DRM_MODE_PAGE_FLIP_EVENT;

    if (ioctl(disp->fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) == 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(disp->fd, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        if (select(disp->fd + 1, &fds, NULL, NULL, &tv) > 0) {
            char ev_buf[32];
            ssize_t n = read(disp->fd, ev_buf, sizeof(ev_buf));
            (void)n;
        }
        return 0;
    }
    struct drm_mode_fb_dirty_cmd dirty = { .fb_id = disp->fb_id, .flags = 0 };
    return ioctl(disp->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

void bb_display_close(bb_display_t *disp) {
    if (disp->fd < 0) return;

    if (disp->restored && disp->saved_fb) {
        struct drm_mode_crtc crtc = {0};
        crtc.crtc_id = disp->crtc_id;
        ioctl(disp->fd, DRM_IOCTL_MODE_GETCRTC, &crtc);
        crtc.fb_id = disp->saved_fb;
        crtc.mode_valid = 1;
        ioctl(disp->fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
    }

    if (disp->is_master) { ioctl(disp->fd, DRM_IOCTL_DROP_MASTER, 0); }

    if (disp->map)      { munmap(disp->map, disp->size); disp->map = NULL; }
    if (disp->fb_id)    { struct drm_mode_fb_cmd2 t = { .fb_id = disp->fb_id };
                          ioctl(disp->fd, DRM_IOCTL_MODE_RMFB, &t); disp->fb_id = 0; }
    if (disp->handle)   { struct drm_mode_destroy_dumb t = { .handle = disp->handle };
                          ioctl(disp->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &t); disp->handle = 0; }
    close(disp->fd);
    disp->fd = -1;
}
