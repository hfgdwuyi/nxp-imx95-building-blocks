#include "bb_persist.h"
#include "bb_board.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __linux__
#include <sys/mount.h>
#endif

#define PERSIST_ROOT   "/persist"
#define MFG_DEVICE     BB_ROOT_DEV "p" BB_STR(BB_PART_MFG)
#define PERSIST_DEVICE BB_ROOT_DEV "p" BB_STR(BB_PART_PERSIST)
#define BOOT_LOG_PATH  "/persist/boot-count.log"
#define UPDATE_LOG_PATH "/persist/update-log.json"
#define MAX_BOOT_LOG   256
#define MAX_UPDATE_LOG 32

#define BB_STR_(x) #x
#define BB_STR(x)  BB_STR_(x)

static bb_mfg_data_t g_mfg;
static int g_mfg_loaded = 0;

static int ensure_mounted(const char *device, const char *target, const char *fstype)
{
    struct stat st;
    if (stat(target, &st) != 0) {
        mkdir(target, 0755);
    }

    FILE *f = fopen("/proc/mounts", "r");
    if (f) {
        char line[512];
        int mounted = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, target)) { mounted = 1; break; }
        }
        fclose(f);
        if (mounted) return 0;
    }

#ifdef __linux__
    if (mount(device, target, fstype, 0, NULL) != 0) {
        return -errno;
    }
#else
    (void)device; (void)target; (void)fstype;
    return -1;
#endif
    return 0;
}

int bb_persist_init(void)
{
    return ensure_mounted(PERSIST_DEVICE, PERSIST_ROOT, "ext4");
}

char *bb_persist_config_get(const char *ns, const char *key)
{
    char path[256];
    snprintf(path, sizeof(path), PERSIST_ROOT "/config/%s.conf", ns);

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    char line[512];
    char *value = NULL;
    size_t klen = strlen(key);

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            char *v = line + klen + 1;
            size_t vlen = strlen(v);
            while (vlen > 0 && (v[vlen-1] == '\n' || v[vlen-1] == '\r'))
                vlen--;
            value = strndup(v, vlen);
            break;
        }
    }
    fclose(f);
    return value;
}

int bb_persist_config_set(const char *ns, const char *key, const char *value)
{
    char path[256];
    snprintf(path, sizeof(path), PERSIST_ROOT "/config");

    mkdir(path, 0755);
    snprintf(path, sizeof(path), PERSIST_ROOT "/config/%s.conf", ns);

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *in = fopen(path, "r");
    FILE *out = fopen(tmp, "w");
    if (!out) return -1;

    int replaced = 0;
    if (in) {
        char line[512];
        size_t klen = strlen(key);
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
                fprintf(out, "%s=%s\n", key, value);
                replaced = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!replaced) {
        fprintf(out, "%s=%s\n", key, value);
    }
    fclose(out);

    rename(tmp, path);
    return 0;
}

const char *bb_persist_machine_id(void)
{
    static char id[64];
    char path[128];
    snprintf(path, sizeof(path), PERSIST_ROOT "/machine-id");

    FILE *f = fopen(path, "r");
    if (!f) return "unknown";
    if (!fgets(id, sizeof(id), f)) { fclose(f); return "unknown"; }
    fclose(f);

    id[strcspn(id, "\n")] = '\0';
    return id;
}

int bb_persist_boot_log_add(const bb_boot_entry_t *entry)
{
    FILE *f = fopen(BOOT_LOG_PATH, "a");
    if (!f) return -1;

    fprintf(f, "%llu %c %d %u\n",
            (unsigned long long)entry->timestamp,
            entry->slot, entry->success, entry->boot_time_ms);
    fclose(f);
    return 0;
}

int bb_persist_boot_log_read(bb_boot_entry_t *entries, int max_entries)
{
    FILE *f = fopen(BOOT_LOG_PATH, "r");
    if (!f) return 0;

    int count = 0;
    char line[256];
    while (count < max_entries && fgets(line, sizeof(line), f)) {
        unsigned long long ts;
        char slot;
        int success;
        unsigned int ms;
        if (sscanf(line, "%llu %c %d %u", &ts, &slot, &success, &ms) == 4) {
            entries[count].timestamp = ts;
            entries[count].slot = slot;
            entries[count].success = success;
            entries[count].boot_time_ms = ms;
            count++;
        }
    }
    fclose(f);
    return count;
}

int bb_persist_update_log_add(const bb_update_entry_t *entry)
{
    FILE *f = fopen(UPDATE_LOG_PATH, "a");
    if (!f) return -1;

    fprintf(f, "{\"ts\":%llu,\"from\":\"%s\",\"to\":\"%s\",\"slot\":\"%c\",\"ok\":%d}\n",
            (unsigned long long)entry->timestamp,
            entry->from_version, entry->to_version,
            entry->target_slot, entry->success);
    fclose(f);
    return 0;
}

int bb_persist_update_log_read(bb_update_entry_t *entries, int max_entries)
{
    FILE *f = fopen(UPDATE_LOG_PATH, "r");
    if (!f) return 0;

    int count = 0;
    char line[512];
    while (count < max_entries && fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "\"ts\":");
        if (p) entries[count].timestamp = strtoull(p + 5, NULL, 10);

        p = strstr(line, "\"from\":\"");
        if (p) {
            char *start = p + 8;
            char *end = strchr(start, '"');
            if (end && end - start < 32) {
                memcpy(entries[count].from_version, start, end - start);
                entries[count].from_version[end - start] = '\0';
            }
        }

        p = strstr(line, "\"to\":\"");
        if (p) {
            char *start = p + 6;
            char *end = strchr(start, '"');
            if (end && end - start < 32) {
                memcpy(entries[count].to_version, start, end - start);
                entries[count].to_version[end - start] = '\0';
            }
        }

        p = strstr(line, "\"slot\":\"");
        if (p) entries[count].target_slot = p[8];

        p = strstr(line, "\"ok\":");
        if (p) entries[count].success = (p[5] == 't');

        count++;
    }
    fclose(f);
    return count;
}

// Manufacturing data
int bb_mfg_read(bb_mfg_data_t *out)
{
    if (g_mfg_loaded) {
        memcpy(out, &g_mfg, sizeof(*out));
        return 0;
    }

    int fd = open(MFG_DEVICE, O_RDONLY);
    if (fd < 0) return -1;

    ssize_t n = read(fd, out, sizeof(*out));
    close(fd);

    if (n != sizeof(*out)) return -1;

    uint32_t crc = 0;
    const uint8_t *p = (const uint8_t *)out;
    for (size_t i = 0; i < sizeof(*out) - 4; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
    }
    if (crc != out->crc32) return -1;

    memcpy(&g_mfg, out, sizeof(*out));
    g_mfg_loaded = 1;
    return 0;
}

const char *bb_mfg_serial(void)
{
    if (!g_mfg_loaded) bb_mfg_read(&g_mfg);
    return g_mfg.serial;
}

const char *bb_mfg_hw_revision(void)
{
    if (!g_mfg_loaded) bb_mfg_read(&g_mfg);
    return g_mfg.hw_revision;
}

const char *bb_mfg_part_number(void)
{
    if (!g_mfg_loaded) bb_mfg_read(&g_mfg);
    return g_mfg.part_number;
}
