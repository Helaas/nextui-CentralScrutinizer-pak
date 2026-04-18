#ifndef CS_STATES_H
#define CS_STATES_H

#include <stddef.h>

#include "cs_paths.h"
#include "cs_platforms.h"

#define CS_STATE_MAX_ENTRIES 256
#define CS_STATE_SLOT_MAX 99
#define CS_STATE_MAX_PATHS 12
#define CS_STATE_MAX_WARNINGS 4

typedef struct cs_state_entry {
    char id[256];
    char title[256];
    char core_dir[256];
    int slot;
    char slot_label[32];
    char kind[32];
    char format[32];
    long long modified;
    unsigned long long size;
    char preview_path[CS_PATH_MAX];
    char download_paths[CS_STATE_MAX_PATHS][CS_PATH_MAX];
    size_t download_path_count;
    char delete_paths[CS_STATE_MAX_PATHS][CS_PATH_MAX];
    size_t delete_path_count;
    char warnings[CS_STATE_MAX_WARNINGS][128];
    size_t warning_count;
} cs_state_entry;

/* Collects grouped save-state bundles for a platform.
 * entry_count_out reports the full bundle count; truncated_out reports whether entries was capped.
 */
int cs_states_collect(const cs_paths *paths,
                      const cs_platform_info *platform,
                      cs_state_entry *entries,
                      size_t entry_capacity,
                      size_t *entry_count_out,
                      int *truncated_out);

#endif
