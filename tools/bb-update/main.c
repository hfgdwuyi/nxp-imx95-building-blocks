/*
 * bb-update - OTA Update Management Tool for i.MX95
 *
 * Usage:
 *   bb-update create  [options]            Create .bbu package
 *   bb-update verify  <file.bbu>           Verify package
 *   bb-update install <file.bbu>           Install package
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bb_update.h"

static void usage(void) {
    printf("bb-update - i.MX95 Building Blocks Update Tool\n\n"
           "Usage:\n"
           "  bb-update create  --output <file.bbu> [options]\n"
           "      --version <ver>     Version string (e.g. 2.0.1)\n"
           "      --product <name>    Product name\n"
           "      --slot <a|b>        Target slot\n"
           "      --kernel <Image>    Kernel image path\n"
           "      --dtb <file.dtb>    Device tree path\n"
           "      --rootfs <path>     Rootfs image or directory\n"
           "      --sign-key <key>    RSA private key for signing\n"
           "\n"
           "  bb-update verify  <file.bbu>\n"
           "  bb-update install <file.bbu>\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 0; }

    if (strcmp(argv[1], "create") == 0) {
        bb_update_config_t cfg = {0};
        cfg.slot = '=';
        strcpy(cfg.product, "i.MX95 EVK");

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--version") == 0 && i + 1 < argc)
                strncpy(cfg.version, argv[++i], sizeof(cfg.version) - 1);
            else if (strcmp(argv[i], "--product") == 0 && i + 1 < argc)
                strncpy(cfg.product, argv[++i], sizeof(cfg.product) - 1);
            else if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
                cfg.slot = argv[++i][0];
            else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc)
                strncpy(cfg.kernel_path, argv[++i], sizeof(cfg.kernel_path) - 1);
            else if (strcmp(argv[i], "--dtb") == 0 && i + 1 < argc)
                strncpy(cfg.dtb_path, argv[++i], sizeof(cfg.dtb_path) - 1);
            else if (strcmp(argv[i], "--rootfs") == 0 && i + 1 < argc)
                strncpy(cfg.rootfs_path, argv[++i], sizeof(cfg.rootfs_path) - 1);
            else if (strcmp(argv[i], "--sign-key") == 0 && i + 1 < argc)
                strncpy(cfg.sign_key_path, argv[++i], sizeof(cfg.sign_key_path) - 1);
            else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
                strncpy(cfg.output_path, argv[++i], sizeof(cfg.output_path) - 1);
        }

        if (!cfg.output_path[0] || !cfg.version[0]) {
            fprintf(stderr, "Error: --output and --version are required\n");
            return 1;
        }
        return bb_update_create(&cfg);
    }
    else if (strcmp(argv[1], "verify") == 0 && argc >= 3) {
        return bb_update_verify(argv[2]);
    }
    else if (strcmp(argv[1], "install") == 0 && argc >= 3) {
        return bb_update_install(argv[2]);
    }
    else {
        usage();
    }
    return 0;
}
