#ifndef BB_UPDATE_H
#define BB_UPDATE_H

// OTA Update tool for i.MX95 building blocks
// Creates and installs .bbu (Building Blocks Update) packages

typedef struct {
    char version[32];
    char product[64];
    char slot;              // 'a' or 'b'
    char kernel_path[256];
    char dtb_path[256];
    char rootfs_path[256];
    char sign_key_path[256];
    char output_path[256];
} bb_update_config_t;

// Create a .bbu update package
int bb_update_create(const bb_update_config_t *cfg);

// Verify a .bbu package (signature + checksums)
int bb_update_verify(const char *bbu_path);

// Install a .bbu package to the inactive slot
int bb_update_install(const char *bbu_path);

#endif
