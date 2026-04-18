#include "cs_bios.h"
#include "cs_util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct cs_bios_definition {
    const char *tag;
    const char *label;
    const char *file_name;
    const char *directory;
    int is_rom_file;
} cs_bios_definition;

static const cs_bios_definition g_bios_definitions[] = {
    {"FDS", "System BIOS", "disksys.rom", "FDS", 0},
    {"GB", "Boot ROM", "gb_bios.bin", "GB", 0},
    {"GBC", "Boot ROM", "gbc_bios.bin", "GBC", 0},
    {"GBA", "Boot ROM", "gba_bios.bin", "GBA", 0},
    {"A5200", "System BIOS", "5200.rom", "A5200", 0},
    {"A7800", "System BIOS", "7800 BIOS (U).rom", "A7800", 0},
    {"LYNX", "System BIOS", "lynxboot.img", "LYNX", 0},
    {"COLECO", "System BIOS", "colecovision.rom", "COLECO", 0},
    {"SEGACD", "Regional BIOS", "bios_CD_E.bin", "SEGACD", 0},
    {"SEGACD", "Regional BIOS", "bios_CD_J.bin", "SEGACD", 0},
    {"SEGACD", "Regional BIOS", "bios_CD_U.bin", "SEGACD", 0},
    {"PS", "System BIOS", "scph1001.bin", "PS", 0},
    {"NEOGEO", "Arcade BIOS", "neogeo.zip", "Arcade (FBN)", 1},
};

static int cs_file_exists(const char *path) {
    return path && access(path, F_OK) == 0;
}

int cs_bios_collect_requirements(const cs_paths *paths,
                                 const cs_platform_info *platform,
                                 cs_bios_requirement *requirements,
                                 size_t requirement_capacity,
                                 size_t *requirement_count_out,
                                 cs_bios_summary *summary_out) {
    size_t i;
    size_t count = 0;
    cs_bios_summary summary = {0};

    if (!paths || !platform) {
        return -1;
    }

    for (i = 0; i < sizeof(g_bios_definitions) / sizeof(g_bios_definitions[0]); ++i) {
        const cs_bios_definition *definition = &g_bios_definitions[i];
        const char *root = definition->is_rom_file ? paths->roms_root : paths->bios_root;
        char absolute_path[CS_PATH_MAX];
        int present;

        if (strcmp(definition->tag, platform->tag) != 0) {
            continue;
        }

        if (CS_SAFE_SNPRINTF(absolute_path,
                             sizeof(absolute_path),
                             "%s/%s/%s",
                             root,
                             definition->directory,
                             definition->file_name)
            != 0) {
            return -1;
        }

        present = cs_file_exists(absolute_path);
        summary.required_count += 1;
        if (present) {
            summary.present_count += 1;
        }

        if (requirements && count < requirement_capacity) {
            cs_bios_requirement *requirement = &requirements[count];

            memset(requirement, 0, sizeof(*requirement));
            if (CS_SAFE_SNPRINTF(requirement->label, sizeof(requirement->label), "%s", definition->label) != 0
                || CS_SAFE_SNPRINTF(requirement->file_name, sizeof(requirement->file_name), "%s", definition->file_name)
                       != 0
                || CS_SAFE_SNPRINTF(requirement->path,
                                    sizeof(requirement->path),
                                    "%s/%s",
                                    definition->directory,
                                    definition->file_name)
                       != 0
                || CS_SAFE_SNPRINTF(requirement->status,
                                    sizeof(requirement->status),
                                    "%s",
                                    present ? "present" : "missing")
                       != 0) {
                return -1;
            }
            requirement->required = 1;
        }

        count += 1;
    }

    summary.satisfied = summary.required_count == 0 || summary.present_count == summary.required_count;

    if (requirement_count_out) {
        *requirement_count_out = count;
    }
    if (summary_out) {
        *summary_out = summary;
    }

    return 0;
}
