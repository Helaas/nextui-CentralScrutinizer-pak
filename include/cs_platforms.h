#ifndef CS_PLATFORMS_H
#define CS_PLATFORMS_H

#include <stddef.h>

#include "cs_paths.h"

typedef struct cs_platform_info {
    char tag[32];
    char name[128];
    char group[64];
    char icon[32];
    char primary_code[32];
    char rom_directory[256];
    int is_custom;
} cs_platform_info;

size_t cs_platform_count(void);
const cs_platform_info *cs_platform_at(size_t index);
const cs_platform_info *cs_platform_find(const char *tag);
int cs_platform_copy(const cs_platform_info *source, cs_platform_info *target);
int cs_platform_resolve(const cs_paths *paths, const char *tag, cs_platform_info *target);
int cs_platform_discover(const cs_paths *paths,
                         cs_platform_info *platforms,
                         size_t capacity,
                         size_t *count_out);
int cs_platform_parse_rom_directory(const char *dir_name,
                                    char *system_name,
                                    size_t system_name_size,
                                    char *system_code,
                                    size_t system_code_size);
int cs_platform_supports_resource(const cs_platform_info *platform, const char *resource);
int cs_platform_allows_hidden_rom_entries(const cs_platform_info *platform);
int cs_platform_is_shortcut_directory(const char *name, const char *absolute_path);

#endif
