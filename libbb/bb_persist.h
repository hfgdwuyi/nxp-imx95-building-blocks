#ifndef BB_PERSIST_H
#define BB_PERSIST_H

#include <stddef.h>
#include <stdint.h>

// Persist partition (/persist) — survives A/B updates

int bb_persist_init(void);

char *bb_persist_config_get(const char *ns, const char *key);
int   bb_persist_config_set(const char *ns, const char *key, const char *value);

const char *bb_persist_machine_id(void);

typedef struct {
    uint64_t    timestamp;
    char        slot;
    int         success;
    uint32_t    boot_time_ms;
} bb_boot_entry_t;

int  bb_persist_boot_log_add(const bb_boot_entry_t *entry);
int  bb_persist_boot_log_read(bb_boot_entry_t *entries, int max_entries);

typedef struct {
    uint64_t    timestamp;
    char        from_version[32];
    char        to_version[32];
    char        target_slot;
    int         success;
} bb_update_entry_t;

int  bb_persist_update_log_add(const bb_update_entry_t *entry);
int  bb_persist_update_log_read(bb_update_entry_t *entries, int max_entries);

// Manufacturing partition (/mfg) — read-only

#define BB_MFG_SERIAL_LEN     32
#define BB_MFG_MAC_LEN         6
#define BB_MFG_HW_REV_LEN     32
#define BB_MFG_DATE_LEN       16
#define BB_MFG_PARTNO_LEN     32
#define BB_MFG_CERT_FP_LEN    64

typedef struct __attribute__((packed)) {
    char     serial[BB_MFG_SERIAL_LEN];
    uint8_t  mac[BB_MFG_MAC_LEN];
    uint8_t  mac_wifi[BB_MFG_MAC_LEN];
    char     hw_revision[BB_MFG_HW_REV_LEN];
    char     mfg_date[BB_MFG_DATE_LEN];
    char     part_number[BB_MFG_PARTNO_LEN];
    char     cert_fingerprint[BB_MFG_CERT_FP_LEN];
    uint32_t crc32;
} bb_mfg_data_t;

int bb_mfg_read(bb_mfg_data_t *out);
const char *bb_mfg_serial(void);
const char *bb_mfg_hw_revision(void);
const char *bb_mfg_part_number(void);

#endif
