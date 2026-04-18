#ifndef CS_BIOS_H
#define CS_BIOS_H

#include <stddef.h>

#include "cs_paths.h"
#include "cs_platforms.h"

#define CS_BIOS_MAX_REQUIREMENTS 16

typedef struct cs_bios_requirement {
    char label[128];
    char file_name[128];
    char path[CS_PATH_MAX];
    char status[32];
    int required;
} cs_bios_requirement;

typedef struct cs_bios_summary {
    size_t required_count;
    size_t present_count;
    int satisfied;
} cs_bios_summary;

int cs_bios_collect_requirements(const cs_paths *paths,
                                 const cs_platform_info *platform,
                                 cs_bios_requirement *requirements,
                                 size_t requirement_capacity,
                                 size_t *requirement_count_out,
                                 cs_bios_summary *summary_out);

#endif
