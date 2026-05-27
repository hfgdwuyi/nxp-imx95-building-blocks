#ifndef BB_RECOVERY_H
#define BB_RECOVERY_H

#include <stdint.h>

typedef enum {
    BB_RECOVERY_NONE       = 0,
    BB_RECOVERY_REQUESTED  = 1,
    BB_RECOVERY_SLOT_FAIL  = 2,
    BB_RECOVERY_WATCHDOG   = 3,
    BB_RECOVERY_FACTORY    = 4,
} bb_recovery_reason_t;

int bb_recovery_request(bb_recovery_reason_t reason);
int bb_recovery_is_active(void);
bb_recovery_reason_t bb_recovery_reason(void);

int bb_recovery_verify_partitions(void);
int bb_recovery_reimage(const char *partition, const char *image_path,
                        void (*progress_cb)(int percent));
int bb_recovery_restore_snapshot(const char *snapshot_label);
char *bb_recovery_list_snapshots(void);
int bb_recovery_factory_reset(void);

int bb_snapshot_create(const char *label);
int bb_snapshot_delete(const char *label);

#endif
