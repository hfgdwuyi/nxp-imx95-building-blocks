#include "bb_recovery.h"
#include "bb_board.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifdef __linux__
#include <sys/mount.h>
#endif

#define RECOVERY_DEVICE    BB_ROOT_DEV "p" BB_STR(BB_PART_RECOVERY)
#define RECOVERY_MOUNT     "/recovery"
#define SNAPSHOT_DIR       "/recovery/snapshots"
#define RECOVERY_PART_P    BB_ROOT_DEV "p" BB_STR(BB_PART_RECOVERY)

#define BB_STR_(x) #x
#define BB_STR(x)  BB_STR_(x)

#ifdef __linux__
#define bb_mount(src, tgt, fst) mount(src, tgt, fst, 0, NULL)
#else
#define bb_mount(src, tgt, fst) ((void)(src), (void)(tgt), (void)(fst), -1)
#endif

int bb_recovery_request(bb_recovery_reason_t reason)
{
    (void)reason;
    FILE *f = popen("fw_setenv recovery_mode 1", "r");
    if (!f) return -1;
    int rc = pclose(f);
    return (rc == 0) ? 0 : -1;
}

int bb_recovery_is_active(void)
{
    struct stat st;
    if (stat("/", &st) != 0) return 0;

    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) return 0;

    char cmdline[512];
    int active = 0;
    if (fgets(cmdline, sizeof(cmdline), f)) {
        active = (strstr(cmdline, "root=" RECOVERY_PART_P) != NULL);
    }
    fclose(f);
    return active;
}

bb_recovery_reason_t bb_recovery_reason(void)
{
    FILE *f = fopen("/persist/recovery_reason", "r");
    if (!f) return BB_RECOVERY_NONE;

    int r = BB_RECOVERY_NONE;
    fscanf(f, "%d", &r);
    fclose(f);
    return (bb_recovery_reason_t)r;
}

int bb_recovery_verify_partitions(void)
{
    char boot_a[32], boot_b[32], root_a[32], root_b[32];
    char persist_p[32], log_p[32];
    snprintf(boot_a, sizeof(boot_a), BB_ROOT_DEV "p%d", BB_PART_BOOT_A);
    snprintf(boot_b, sizeof(boot_b), BB_ROOT_DEV "p%d", BB_PART_BOOT_B);
    snprintf(root_a, sizeof(root_a), BB_ROOT_DEV "p%d", BB_PART_ROOTFS_A);
    snprintf(root_b, sizeof(root_b), BB_ROOT_DEV "p%d", BB_PART_ROOTFS_B);
    snprintf(persist_p, sizeof(persist_p), BB_ROOT_DEV "p%d", BB_PART_PERSIST);
    snprintf(log_p, sizeof(log_p), BB_ROOT_DEV "p%d", BB_PART_LOG);

    const char *parts[] = { boot_a, boot_b, root_a, root_b, persist_p, log_p, NULL };

    int failures = 0;
    for (int i = 0; parts[i]; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "fsck.ext4 -n %s >/dev/null 2>&1", parts[i]);

        if (strstr(parts[i], "p4") || strstr(parts[i], "p5")) {
            snprintf(cmd, sizeof(cmd), "fsck.vfat -n %s >/dev/null 2>&1", parts[i]);
        }

        int rc = system(cmd);
        if (rc != 0) {
            printf("Partition %s: FAILED\n", parts[i]);
            failures++;
        } else {
            printf("Partition %s: OK\n", parts[i]);
        }
    }
    return failures;
}

int bb_recovery_reimage(const char *partition, const char *image_path,
                        void (*progress_cb)(int percent))
{
    (void)progress_cb;

    printf("Re-imaging %s from %s ...\n", partition, image_path);

    int is_tar = (strstr(image_path, ".tar") != NULL ||
                  strstr(image_path, ".tgz") != NULL);

    char cmd[512];
    if (is_tar) {
        snprintf(cmd, sizeof(cmd),
                 "mkfs.ext4 -F %s && mkdir -p /tmp/reimage && "
                 "mount %s /tmp/reimage && "
                 "tar xzf %s -C /tmp/reimage && "
                 "umount /tmp/reimage",
                 partition, partition, image_path);
    } else {
        snprintf(cmd, sizeof(cmd), "dd if=%s of=%s bs=4M status=progress",
                 image_path, partition);
    }

    int rc = system(cmd);
    printf("%s: %s\n", partition, (rc == 0) ? "OK" : "FAILED");
    return rc;
}

int bb_recovery_restore_snapshot(const char *snapshot_label)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SNAPSHOT_DIR, snapshot_label);

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Snapshot '%s' not found\n", snapshot_label);
        return -1;
    }

    mkdir(RECOVERY_MOUNT, 0755);
    bb_mount(RECOVERY_DEVICE, RECOVERY_MOUNT, "ext4");

    char manifest[256];
    snprintf(manifest, sizeof(manifest), "%s/manifest.txt", path);
    FILE *f = fopen(manifest, "r");
    if (!f) {
        printf("Snapshot manifest not found\n");
        return -1;
    }

    char line[256];
    int errors = 0;
    while (fgets(line, sizeof(line), f)) {
        char part[128], img[128];
        if (sscanf(line, "%127s %127s", part, img) == 2) {
            char img_path[256];
            snprintf(img_path, sizeof(img_path), "%s/%s", path, img);
            if (bb_recovery_reimage(part, img_path, NULL) != 0)
                errors++;
        }
    }
    fclose(f);

    printf("Snapshot restore: %d errors\n", errors);
    return errors;
}

char *bb_recovery_list_snapshots(void)
{
    mkdir(RECOVERY_MOUNT, 0755);
    bb_mount(RECOVERY_DEVICE, RECOVERY_MOUNT, "ext4");

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "ls -1 %s 2>/dev/null | sed 's/.*/\"&\"/' | paste -sd, -",
             SNAPSHOT_DIR);

    FILE *f = popen(cmd, "r");
    if (!f) return strdup("[]");

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    pclose(f);

    if (len == 0) return strdup("[]");

    buf[len] = '\0';
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    char *result = malloc(len + 3);
    snprintf(result, len + 3, "[%s]", buf);
    return result;
}

int bb_recovery_factory_reset(void)
{
    printf("=== FACTORY RESET ===\n");
    printf("This will erase all user data and restore factory state.\n");

    bb_recovery_reimage("/dev/mmcblk0p6", "/recovery/factory/rootfs-a.img", NULL);
    bb_recovery_reimage("/dev/mmcblk0p7", "/recovery/factory/rootfs-b.img", NULL);

    system("fw_setenv boot_slot a");
    system("fw_setenv boot_attempt 3");
    system("fw_setenv boot_ok 0");
    system("fw_setenv recovery_mode 0");

    system("mkfs.ext4 -F /dev/mmcblk0p9");
    system("mkfs.ext4 -F /dev/mmcblk0p11");

    printf("Factory reset complete. Reboot to boot slot A.\n");
    return 0;
}

int bb_snapshot_create(const char *label)
{
    mkdir(RECOVERY_MOUNT, 0755);

    if (bb_mount(RECOVERY_DEVICE, RECOVERY_MOUNT, "ext4") != 0)
        return -errno;

    char snapdir[256];
    snprintf(snapdir, sizeof(snapdir), "%s/%s", SNAPSHOT_DIR, label);
    mkdir(SNAPSHOT_DIR, 0755);
    mkdir(snapdir, 0755);

    char manifest[256];
    snprintf(manifest, sizeof(manifest), "%s/manifest.txt", snapdir);
    FILE *f = fopen(manifest, "w");
    if (!f) return -1;

    fprintf(f, "/dev/mmcblk0p9 persist.tar.gz\n");
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "tar czf %s/persist.tar.gz -C /persist . 2>/dev/null",
             snapdir);
    system(cmd);

    snprintf(cmd, sizeof(cmd),
             "cp /etc/os-release %s/os-release 2>/dev/null",
             snapdir);
    system(cmd);

    printf("Snapshot '%s' created\n", label);
    return 0;
}

int bb_snapshot_delete(const char *label)
{
    mkdir(RECOVERY_MOUNT, 0755);
    bb_mount(RECOVERY_DEVICE, RECOVERY_MOUNT, "ext4");

    char snapdir[256];
    snprintf(snapdir, sizeof(snapdir), "%s/%s", SNAPSHOT_DIR, label);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", snapdir);
    return system(cmd);
}
