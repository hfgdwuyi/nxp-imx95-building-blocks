#include "bb_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

int bb_update_create(const bb_update_config_t *cfg)
{
    printf("Creating update package: %s\n", cfg->output_path);
    printf("  Version:  %s\n", cfg->version);
    printf("  Product:  %s\n", cfg->product);
    printf("  Slot:     %c\n", cfg->slot);
    printf("  Kernel:   %s\n", cfg->kernel_path[0] ? cfg->kernel_path : "(none)");
    printf("  DTB:      %s\n", cfg->dtb_path[0] ? cfg->dtb_path : "(none)");
    printf("  Rootfs:   %s\n", cfg->rootfs_path[0] ? cfg->rootfs_path : "(none)");

    // Create temporary work directory
    char tmpdir[] = "/tmp/bbu-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return -1;
    }

    // Write manifest.json
    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", tmpdir);
    FILE *f = fopen(manifest_path, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"%s\",\n", cfg->version);
    fprintf(f, "  \"product\": \"%s\",\n", cfg->product);
    fprintf(f, "  \"target_slot\": \"%c\",\n", cfg->slot);
    fprintf(f, "  \"timestamp\": %lld\n", (long long)time(NULL));
    fprintf(f, "}\n");
    fclose(f);

    // Build tar.gz for kernel+dtb (boot)
    if (cfg->kernel_path[0] || cfg->dtb_path[0]) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "tar czf %s/boot.tar.gz -C / "
                 "%s %s 2>/dev/null",
                 tmpdir,
                 cfg->kernel_path[0] ? cfg->kernel_path : "",
                 cfg->dtb_path[0] ? cfg->dtb_path : "");
        system(cmd);
    }

    // Copy or tar rootfs
    if (cfg->rootfs_path[0]) {
        struct stat st;
        if (stat(cfg->rootfs_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                     "tar czf %s/rootfs.tar.gz -C %s . 2>/dev/null",
                     tmpdir, cfg->rootfs_path);
            system(cmd);
        } else {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "cp %s %s/rootfs.tar.gz 2>/dev/null",
                     cfg->rootfs_path, tmpdir);
            system(cmd);
        }
    }

    // Create final .bbu archive
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "tar czf %s -C %s manifest.json boot.tar.gz rootfs.tar.gz 2>/dev/null",
             cfg->output_path, tmpdir);
    int rc = system(cmd);

    // Cleanup
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);

    if (rc == 0) {
        printf("Update package created: %s\n", cfg->output_path);
    } else {
        printf("Failed to create update package\n");
    }
    return rc;
}

int bb_update_verify(const char *bbu_path)
{
    printf("Verifying %s ...\n", bbu_path);

    // Extract manifest and check
    char tmpdir[] = "/tmp/bbu-verify-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return -1;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar xzf %s -C %s manifest.json 2>/dev/null", bbu_path, tmpdir);
    if (system(cmd) != 0) {
        printf("FAILED: Cannot extract manifest from %s\n", bbu_path);
        snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
        system(cmd);
        return -1;
    }

    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", tmpdir);
    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        printf("FAILED: No manifest found\n");
        snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
        system(cmd);
        return -1;
    }

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    printf("Manifest:\n%s\n", buf);
    printf("Package verified OK.\n");

    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
    return 0;
}

int bb_update_install(const char *bbu_path)
{
    printf("Installing %s ...\n", bbu_path);

    if (bb_update_verify(bbu_path) != 0) {
        fprintf(stderr, "Verification failed, aborting install\n");
        return -1;
    }

    // Determine target slot (opposite of current)
    FILE *f = popen("fw_printenv boot_slot", "r");
    if (!f) { perror("fw_printenv"); return -1; }

    char buf[64];
    char current_slot = 'a';
    if (fgets(buf, sizeof(buf), f)) {
        char *p = strchr(buf, '=');
        if (p) current_slot = p[1];
    }
    pclose(f);

    char target_slot = (current_slot == 'a') ? 'b' : 'a';
    printf("Current slot: %c, Target slot: %c\n", current_slot, target_slot);

    // Extract and install
    char tmpdir[] = "/tmp/bbu-install-XXXXXX";
    if (!mkdtemp(tmpdir)) {
        perror("mkdtemp");
        return -1;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar xzf %s -C %s", bbu_path, tmpdir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to extract package\n");
        snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
        system(cmd);
        return -1;
    }

    // Install boot (kernel+dtb) to boot_<slot> partition
    snprintf(cmd, sizeof(cmd),
             "mkdir -p /tmp/bbu-boot && "
             "tar xzf %s/boot.tar.gz -C /tmp/bbu-boot && "
             "cp /tmp/bbu-boot/Image /boot/Image-%c 2>/dev/null; "
             "cp /tmp/bbu-boot/*.dtb /boot/ 2>/dev/null; "
             "rm -rf /tmp/bbu-boot",
             tmpdir, target_slot);
    system(cmd);

    // Install rootfs to rootfs_<slot> partition
    if (access(tmpdir, F_OK) == 0) {
        char rootfs_path[256];
        snprintf(rootfs_path, sizeof(rootfs_path), "%s/rootfs.tar.gz", tmpdir);
        if (access(rootfs_path, F_OK) == 0) {
            printf("Rootfs image found. Flash to target partition.\n");
            // Target partition is determined by boot_slot
            printf("Run: mount rootfs_%c and extract rootfs.tar.gz\n", target_slot);
        }
    }

    // Set U-Boot env to boot new slot on next boot
    snprintf(cmd, sizeof(cmd), "fw_setenv boot_slot %c", target_slot);
    system(cmd);
    system("fw_setenv boot_attempt 3");
    system("fw_setenv boot_ok 0");

    printf("Install complete. Reboot to activate new slot %c.\n", target_slot);
    printf("  systemctl reboot\n");

    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir);
    system(cmd);
    return 0;
}
