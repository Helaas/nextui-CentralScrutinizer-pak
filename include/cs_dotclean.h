#ifndef CS_DOTCLEAN_H
#define CS_DOTCLEAN_H

#include <stddef.h>

#include "cs_paths.h"

#define CS_DOTCLEAN_MAX_DEPTH 32

typedef struct cs_dotclean_entry {
    char path[CS_PATH_MAX];
    char kind[32];
    char reason[128];
    unsigned long long size;
    long long modified;
} cs_dotclean_entry;

/* Scans the SD card for safe macOS transfer artifacts while skipping trusted large trees.
 * entry_count_out reports the full match count; truncated_out reports whether entries was capped.
 */
int cs_dotclean_scan(const cs_paths *paths,
                     cs_dotclean_entry *entries,
                     size_t entry_capacity,
                     size_t *entry_count_out,
                     int *truncated_out);

#endif
