#include "cs_platforms.h"
#include "cs_build_info.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define CS_DISCOVERED_PLATFORM_MAX 128
#define CS_SHORTCUT_MARKER ".shortcut"
#define CS_SHORTCUT_PREFIX "\xEF\xBB\xBF"
/* Keep prefix-only detection for legacy shortcuts that predate the .shortcut marker file. */
#define CS_LEGACY_SHORTCUT_PREFIX "\xE2\x98\x85 "

static const cs_platform_info g_platforms[] = {
    {.tag = "PUAE",
     .name = "Amiga",
     .group = "Computer",
     .icon = "AMIGA",
     .primary_code = "PUAE",
     .rom_directory = "Amiga (PUAE)"},
    {.tag = "CPC",
     .name = "Amstrad CPC",
     .group = "Computer",
     .icon = "CPC",
     .primary_code = "CPC",
     .rom_directory = "Amstrad CPC (CPC)"},
    {.tag = "C128",
     .name = "Commodore 128",
     .group = "Computer",
     .icon = "C128",
     .primary_code = "C128",
     .rom_directory = "Commodore 128 (C128)"},
    {.tag = "C64",
     .name = "Commodore 64",
     .group = "Computer",
     .icon = "C64",
     .primary_code = "C64",
     .rom_directory = "Commodore 64 (C64)"},
    {.tag = "PET",
     .name = "Commodore PET",
     .group = "Computer",
     .icon = "PET",
     .primary_code = "PET",
     .rom_directory = "Commodore PET (PET)"},
    {.tag = "PLUS4",
     .name = "Commodore Plus4",
     .group = "Computer",
     .icon = "PLUS4",
     .primary_code = "PLUS4",
     .rom_directory = "Commodore Plus4 (PLUS4)"},
    {.tag = "VIC",
     .name = "Commodore VIC20",
     .group = "Computer",
     .icon = "VIC20",
     .primary_code = "VIC",
     .rom_directory = "Commodore VIC20 (VIC)"},
    {.tag = "MSX",
     .name = "Microsoft MSX",
     .group = "Computer",
     .icon = "MSX",
     .primary_code = "MSX",
     .rom_directory = "Microsoft MSX (MSX)"},
    {.tag = "PRBOOM",
     .name = "Doom",
     .group = "Computer",
     .icon = "DOOM",
     .primary_code = "PRBOOM",
     .rom_directory = "Doom (PRBOOM)"},
    {.tag = "P8",
     .name = "Pico-8",
     .group = "Computer",
     .icon = "PICO8",
     .primary_code = "P8",
     .rom_directory = "Pico-8 (P8)"},
    {.tag = "PORTS",
     .name = "Ports",
     .group = "PortMaster",
     .icon = "PORTMASTER",
     .primary_code = "PORTS",
     .rom_directory = "Ports (PORTS)"},
    {.tag = "FBN",
     .name = "Arcade",
     .group = "Arcade",
     .icon = "FBN",
     .primary_code = "FBN",
     .rom_directory = "Arcade (FBN)"},
    {.tag = "A2600",
     .name = "Atari 2600",
     .group = "Atari",
     .icon = "ATARI2600",
     .primary_code = "A2600",
     .rom_directory = "Atari 2600 (A2600)"},
    {.tag = "A5200",
     .name = "Atari 5200",
     .group = "Atari",
     .icon = "ATARI5200",
     .primary_code = "A5200",
     .rom_directory = "Atari 5200 (A5200)"},
    {.tag = "A7800",
     .name = "Atari 7800",
     .group = "Atari",
     .icon = "ATARI7800",
     .primary_code = "A7800",
     .rom_directory = "Atari 7800 (A7800)"},
    {.tag = "LYNX",
     .name = "Atari Lynx",
     .group = "Atari",
     .icon = "LYNX",
     .primary_code = "LYNX",
     .rom_directory = "Atari Lynx (LYNX)"},
    {.tag = "FDS",
     .name = "Famicom Disk System",
     .group = "Nintendo",
     .icon = "FDS",
     .primary_code = "FDS",
     .rom_directory = "Famicom Disk System (FDS)"},
    {.tag = "GB",
     .name = "Game Boy",
     .group = "Nintendo",
     .icon = "GB",
     .primary_code = "GB",
     .rom_directory = "Game Boy (GB)"},
    {.tag = "GBA",
     .name = "Game Boy Advance",
     .group = "Nintendo",
     .icon = "GBA",
     .primary_code = "GBA",
     .rom_directory = "Game Boy Advance (GBA)"},
    {.tag = "MGBA",
     .name = "Game Boy Advance",
     .group = "Nintendo",
     .icon = "GBA",
     .primary_code = "MGBA",
     .rom_directory = "Game Boy Advance (MGBA)"},
    {.tag = "GBC",
     .name = "Game Boy Color",
     .group = "Nintendo",
     .icon = "GBC",
     .primary_code = "GBC",
     .rom_directory = "Game Boy Color (GBC)"},
    {.tag = "FC",
     .name = "NES/Famicom",
     .group = "Nintendo",
     .icon = "NES",
     .primary_code = "FC",
     .rom_directory = "Nintendo Entertainment System (FC)"},
    {.tag = "PKM",
     .name = "Pokemon mini",
     .group = "Nintendo",
     .icon = "POKEMINI",
     .primary_code = "PKM",
     .rom_directory = "Pokémon mini (PKM)"},
    {.tag = "SFC",
     .name = "SNES",
     .group = "Nintendo",
     .icon = "SNES",
     .primary_code = "SFC",
     .rom_directory = "Super Nintendo Entertainment System (SFC)"},
    {.tag = "SUPA",
     .name = "SNES",
     .group = "Nintendo",
     .icon = "SNES",
     .primary_code = "SUPA",
     .rom_directory = "Super Nintendo Entertainment System (SUPA)"},
    {.tag = "SGB",
     .name = "Super Game Boy",
     .group = "Nintendo",
     .icon = "GB",
     .primary_code = "SGB",
     .rom_directory = "Super Game Boy (SGB)"},
    {.tag = "VB",
     .name = "Virtual Boy",
     .group = "Nintendo",
     .icon = "VIRTUALBOY",
     .primary_code = "VB",
     .rom_directory = "Virtual Boy (VB)"},
    {.tag = "32X",
     .name = "Sega 32X",
     .group = "Sega",
     .icon = "32X",
     .primary_code = "32X",
     .rom_directory = "Sega 32X (32X)"},
    {.tag = "SEGACD",
     .name = "Sega CD",
     .group = "Sega",
     .icon = "SEGACD",
     .primary_code = "SEGACD",
     .rom_directory = "Sega CD (SEGACD)"},
    {.tag = "GG",
     .name = "Sega Game Gear",
     .group = "Sega",
     .icon = "GG",
     .primary_code = "GG",
     .rom_directory = "Sega Game Gear (GG)"},
    {.tag = "MD",
     .name = "Sega Genesis",
     .group = "Sega",
     .icon = "MD",
     .primary_code = "MD",
     .rom_directory = "Sega Genesis (MD)"},
    {.tag = "SMS",
     .name = "Sega Master System",
     .group = "Sega",
     .icon = "SMS",
     .primary_code = "SMS",
     .rom_directory = "Sega Master System (SMS)"},
    {.tag = "SG1000",
     .name = "Sega SG-1000",
     .group = "Sega",
     .icon = "SG1000",
     .primary_code = "SG1000",
     .rom_directory = "Sega SG-1000 (SG1000)"},
    {.tag = "PS",
     .name = "Sony PlayStation",
     .group = "Sony",
     .icon = "PS",
     .primary_code = "PS",
     .rom_directory = "Sony PlayStation (PS)"},
    {.tag = "NGP",
     .name = "Neo Geo Pocket",
     .group = "SNK",
     .icon = "NGP",
     .primary_code = "NGP",
     .rom_directory = "Neo Geo Pocket (NGP)"},
    {.tag = "NGPC",
     .name = "Neo Geo Pocket Color",
     .group = "SNK",
     .icon = "NGPC",
     .primary_code = "NGPC",
     .rom_directory = "Neo Geo Pocket Color (NGPC)"},
    {.tag = "PCE",
     .name = "PC Engine",
     .group = "NEC",
     .icon = "PCE",
     .primary_code = "PCE",
     .rom_directory = "TurboGrafx-16 (PCE)"},
    {.tag = "COLECO",
     .name = "Colecovision",
     .group = "Other",
     .icon = "COLECOVISION",
     .primary_code = "COLECO",
     .rom_directory = "Colecovision (COLECO)"},
};

typedef struct cs_discovered_rom_dir {
    char system_name[128];
    char system_code[CS_PLATFORM_CODE_MAX];
    char dir_name[256];
} cs_discovered_rom_dir;

static int cs_write_string(char *dst, size_t size, const char *value) {
    int written;

    if (!dst || size == 0 || !value) {
        return -1;
    }

    written = snprintf(dst, size, "%s", value);
    return (written < 0 || (size_t) written >= size) ? -1 : 0;
}

static int cs_platform_component_is_safe(const char *value) {
    size_t i;

    if (!value || value[0] == '\0') {
        return 0;
    }
    if (value[0] == '.') {
        return 0;
    }
    if ((value[0] == '.' && value[1] == '\0')
        || (value[0] == '.' && value[1] == '.' && value[2] == '\0')) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) value[i];

        if (ch < 0x20 || ch == '/' || ch == '\\') {
            return 0;
        }
    }

    return 1;
}

static int cs_platform_known_index(const char *tag) {
    size_t i;

    if (!tag || tag[0] == '\0') {
        return -1;
    }

    for (i = 0; i < sizeof(g_platforms) / sizeof(g_platforms[0]); ++i) {
        if (strcmp(g_platforms[i].tag, tag) == 0) {
            return (int) i;
        }
    }

    if (strcmp(tag, "NES") == 0) {
        return cs_platform_known_index("FC");
    }
    if (strcmp(tag, "SNES") == 0) {
        return cs_platform_known_index("SFC");
    }
    if (strcmp(tag, "GBA_ALT") == 0) {
        return cs_platform_known_index("MGBA");
    }

    return -1;
}

static int cs_platform_codes_equal(const char *left, const char *right) {
    int left_index;
    int right_index;

    if (!left || !right) {
        return 0;
    }
    if (strcmp(left, right) == 0) {
        return 1;
    }

    left_index = cs_platform_known_index(left);
    right_index = cs_platform_known_index(right);
    return left_index >= 0 && right_index >= 0 && left_index == right_index;
}

static int cs_platform_is_directory(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int cs_platform_is_regular_file_not_symlink(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int cs_platform_name_has_prefix(const char *name, const char *prefix) {
    size_t prefix_len;

    if (!name || !prefix) {
        return 0;
    }

    prefix_len = strlen(prefix);
    return prefix_len > 0 && strncmp(name, prefix, prefix_len) == 0;
}

static int cs_platform_is_ports(const cs_platform_info *platform) {
    return platform && strcmp(platform->tag, "PORTS") == 0;
}

static int cs_platform_find_discovered_by_code(const cs_discovered_rom_dir *dirs,
                                               size_t dir_count,
                                               const char *system_code) {
    size_t i;

    if (!dirs || !system_code) {
        return -1;
    }

    for (i = 0; i < dir_count; ++i) {
        if (cs_platform_codes_equal(dirs[i].system_code, system_code)) {
            return (int) i;
        }
    }

    return -1;
}

static int cs_platform_build_is_handheld(void) {
    const char *platform = cs_build_info_platform_name();

    return platform && (strcmp(platform, "tg5040") == 0 || strcmp(platform, "tg5050") == 0 || strcmp(platform, "my355") == 0);
}

static int cs_platform_ports_directory_exists(const cs_paths *paths) {
    char ports_dir[CS_PATH_MAX];
    int written;

    if (!paths) {
        return 0;
    }

    written = snprintf(ports_dir, sizeof(ports_dir), "%s/%s", paths->roms_root, "Ports (PORTS)");
    if (written < 0 || (size_t) written >= sizeof(ports_dir)) {
        return 0;
    }

    return cs_platform_is_directory(ports_dir);
}

static int cs_platform_add_emulator_code(char codes[][CS_PLATFORM_CODE_MAX],
                                         size_t *count,
                                         size_t capacity,
                                         const char *code) {
    size_t i;

    if (!codes || !count || !code || code[0] == '\0') {
        return -1;
    }

    for (i = 0; i < *count; ++i) {
        if (cs_platform_codes_equal(codes[i], code)) {
            return 0;
        }
    }
    if (*count >= capacity) {
        return -1;
    }

    return cs_write_string(codes[(*count)++], CS_PLATFORM_CODE_MAX, code);
}

static int cs_platform_collect_emulators_from_dir(const char *dir_path,
                                                  char codes[][CS_PLATFORM_CODE_MAX],
                                                  size_t *count,
                                                  size_t capacity) {
    DIR *dir;
    struct dirent *entry;

    if (!dir_path || !codes || !count) {
        return -1;
    }

    dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char code[CS_PLATFORM_CODE_MAX];
        char absolute_path[CS_PATH_MAX];
        size_t name_len;
        struct stat st;

        if (entry->d_name[0] == '.') {
            continue;
        }
        name_len = strlen(entry->d_name);
        if (name_len <= 4 || strcmp(entry->d_name + name_len - 4, ".pak") != 0) {
            continue;
        }
        if (name_len - 4 >= sizeof(code)) {
            continue;
        }
        if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", dir_path, entry->d_name) < 0) {
            continue;
        }
        if (lstat(absolute_path, &st) != 0 || !(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) {
            continue;
        }

        memcpy(code, entry->d_name, name_len - 4);
        code[name_len - 4] = '\0';
        if (cs_platform_add_emulator_code(codes, count, capacity, code) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

static int cs_platform_collect_emulators_from_children(const char *root,
                                                       const char *suffix,
                                                       char codes[][CS_PLATFORM_CODE_MAX],
                                                       size_t *count,
                                                       size_t capacity) {
    DIR *dir;
    struct dirent *entry;

    if (!root || !codes || !count) {
        return -1;
    }

    dir = opendir(root);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_dir[CS_PATH_MAX];
        int written;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (suffix) {
            written = snprintf(child_dir, sizeof(child_dir), "%s/%s/%s", root, entry->d_name, suffix);
        } else {
            written = snprintf(child_dir, sizeof(child_dir), "%s/%s", root, entry->d_name);
        }
        if (written < 0 || (size_t) written >= sizeof(child_dir)) {
            continue;
        }
        if (!cs_platform_is_directory(child_dir)) {
            continue;
        }
        if (cs_platform_collect_emulators_from_dir(child_dir, codes, count, capacity) != 0) {
            closedir(dir);
            return -1;
        }
    }

    closedir(dir);
    return 0;
}

int cs_platform_supports_resource(const cs_platform_info *platform, const char *resource) {
    if (!platform || !resource || resource[0] == '\0') {
        return 0;
    }

    if (strcmp(resource, "roms") == 0) {
        return 1;
    }
    if (cs_platform_is_ports(platform)) {
        return 0;
    }

    return strcmp(resource, "saves") == 0 || strcmp(resource, "states") == 0 || strcmp(resource, "bios") == 0
           || strcmp(resource, "overlays") == 0 || strcmp(resource, "cheats") == 0;
}

int cs_platform_requires_emulator(const cs_platform_info *platform) {
    return platform && !cs_platform_is_ports(platform);
}

int cs_platform_allows_hidden_rom_entries(const cs_platform_info *platform) {
    return cs_platform_is_ports(platform);
}

int cs_platform_is_shortcut_directory(const char *name, const char *absolute_path) {
    char marker_path[CS_PATH_MAX];
    const char *basename = name;
    int written;

    if ((!basename || basename[0] == '\0') && absolute_path) {
        basename = strrchr(absolute_path, '/');
        basename = basename ? basename + 1 : absolute_path;
    }

    if (basename && (cs_platform_name_has_prefix(basename, CS_SHORTCUT_PREFIX)
                     || cs_platform_name_has_prefix(basename, CS_LEGACY_SHORTCUT_PREFIX))) {
        return 1;
    }
    if (!absolute_path || absolute_path[0] == '\0') {
        return 0;
    }

    written = snprintf(marker_path, sizeof(marker_path), "%s/%s", absolute_path, CS_SHORTCUT_MARKER);
    if (written < 0 || (size_t) written >= sizeof(marker_path)) {
        return 0;
    }

    return cs_platform_is_regular_file_not_symlink(marker_path);
}

static int cs_platform_scan_rom_dirs(const cs_paths *paths,
                                     cs_discovered_rom_dir *dirs,
                                     size_t capacity,
                                     size_t *count_out) {
    DIR *dir;
    struct dirent *entry;
    size_t count = 0;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !dirs || capacity == 0) {
        return -1;
    }

    dir = opendir(paths->roms_root);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char system_name[sizeof(dirs[0].system_name)];
        char system_code[sizeof(dirs[0].system_code)];
        char absolute_path[CS_PATH_MAX];

        if (entry->d_name[0] == '.') {
            continue;
        }
        if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", paths->roms_root, entry->d_name) < 0) {
            continue;
        }
        if (!cs_platform_is_directory(absolute_path)) {
            continue;
        }
        if (cs_platform_is_shortcut_directory(entry->d_name, absolute_path)) {
            continue;
        }
        if (cs_platform_parse_rom_directory(entry->d_name,
                                            system_name,
                                            sizeof(system_name),
                                            system_code,
                                            sizeof(system_code))
            != 0) {
            continue;
        }
        if (cs_platform_find_discovered_by_code(dirs, count, system_code) >= 0) {
            continue;
        }
        if (count >= capacity) {
            break;
        }

        if (cs_write_string(dirs[count].system_name, sizeof(dirs[count].system_name), system_name) != 0
            || cs_write_string(dirs[count].system_code, sizeof(dirs[count].system_code), system_code) != 0
            || cs_write_string(dirs[count].dir_name, sizeof(dirs[count].dir_name), entry->d_name) != 0) {
            closedir(dir);
            return -1;
        }

        count += 1;
    }

    closedir(dir);
    if (count_out) {
        *count_out = count;
    }
    return 0;
}

size_t cs_platform_count(void) {
    return sizeof(g_platforms) / sizeof(g_platforms[0]);
}

const cs_platform_info *cs_platform_at(size_t index) {
    if (index >= cs_platform_count()) {
        return NULL;
    }

    return &g_platforms[index];
}

const cs_platform_info *cs_platform_find(const char *tag) {
    int index = cs_platform_known_index(tag);

    if (index < 0) {
        return NULL;
    }

    return &g_platforms[index];
}

int cs_platform_copy(const cs_platform_info *source, cs_platform_info *target) {
    if (!source || !target) {
        return -1;
    }

    *target = *source;
    return 0;
}

int cs_platform_resolve(const cs_paths *paths, const char *tag, cs_platform_info *target) {
    const cs_platform_info *known;
    cs_discovered_rom_dir dirs[CS_DISCOVERED_PLATFORM_MAX];
    size_t dir_count = 0;
    int discovered_index;

    if (!tag || !target) {
        return -1;
    }

    known = cs_platform_find(tag);
    if (known && cs_platform_copy(known, target) != 0) {
        return -1;
    }

    if (!paths) {
        return known ? 0 : -1;
    }
    if (known && cs_platform_is_ports(target)) {
        return cs_platform_ports_directory_exists(paths) ? 0 : -1;
    }
    if (cs_platform_scan_rom_dirs(paths, dirs, CS_DISCOVERED_PLATFORM_MAX, &dir_count) != 0) {
        return -1;
    }

    if (known) {
        discovered_index = cs_platform_find_discovered_by_code(dirs, dir_count, target->tag);
        if (discovered_index >= 0
            && cs_write_string(target->rom_directory,
                               sizeof(target->rom_directory),
                               dirs[discovered_index].dir_name)
                   != 0) {
            return -1;
        }
        return 0;
    }

    discovered_index = cs_platform_find_discovered_by_code(dirs, dir_count, tag);
    (void) discovered_index;
    return -1;
}

int cs_platform_discover(const cs_paths *paths,
                         cs_platform_info *platforms,
                         size_t capacity,
                         size_t *count_out) {
    cs_discovered_rom_dir dirs[CS_DISCOVERED_PLATFORM_MAX];
    size_t dir_count = 0;
    size_t count = 0;
    size_t i;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !platforms || capacity == 0) {
        return -1;
    }
    if (cs_platform_scan_rom_dirs(paths, dirs, CS_DISCOVERED_PLATFORM_MAX, &dir_count) != 0) {
        return -1;
    }

    for (i = 0; i < cs_platform_count() && count < capacity; ++i) {
        if (cs_platform_is_ports(&g_platforms[i]) && !cs_platform_ports_directory_exists(paths)) {
            continue;
        }

        platforms[count] = g_platforms[i];

        {
            int discovered_index =
                cs_platform_find_discovered_by_code(dirs, dir_count, platforms[count].tag);

            if (discovered_index >= 0
                && cs_write_string(platforms[count].rom_directory,
                                   sizeof(platforms[count].rom_directory),
                                   dirs[discovered_index].dir_name)
                       != 0) {
                return -1;
            }
        }

        count += 1;
    }

    if (count_out) {
        *count_out = count;
    }
    return 0;
}

int cs_platform_parse_rom_directory(const char *dir_name,
                                    char *system_name,
                                    size_t system_name_size,
                                    char *system_code,
                                    size_t system_code_size) {
    const char *last_open;
    const char *last_close;
    size_t name_len;
    size_t code_len;

    if (!dir_name || !system_name || system_name_size == 0 || !system_code || system_code_size == 0) {
        return -1;
    }

    last_open = strrchr(dir_name, '(');
    if (!last_open || last_open == dir_name || last_open == dir_name + 1 || *(last_open - 1) != ' ') {
        return -1;
    }

    last_close = strrchr(dir_name, ')');
    if (!last_close || last_close < last_open || last_close[1] != '\0') {
        return -1;
    }

    name_len = (size_t) ((last_open - 1) - dir_name);
    code_len = (size_t) (last_close - (last_open + 1));
    if (name_len == 0 || code_len == 0 || name_len >= system_name_size || code_len >= system_code_size) {
        return -1;
    }

    memcpy(system_name, dir_name, name_len);
    system_name[name_len] = '\0';
    memcpy(system_code, last_open + 1, code_len);
    system_code[code_len] = '\0';
    if (!cs_platform_component_is_safe(system_code)) {
        return -1;
    }
    return 0;
}

int cs_platform_collect_installed_emulators(const cs_paths *paths,
                                            char codes[][CS_PLATFORM_CODE_MAX],
                                            size_t capacity,
                                            size_t *count_out) {
    char target_dir[CS_PATH_MAX];
    size_t count = 0;
    const char *platform_name;

    if (count_out) {
        *count_out = 0;
    }
    if (!paths || !codes || capacity == 0) {
        return -1;
    }

    platform_name = cs_build_info_platform_name();
    if (cs_platform_build_is_handheld()) {
        if (snprintf(target_dir, sizeof(target_dir), "%s/%s", paths->emus_root, platform_name) < 0) {
            return -1;
        }
        if (cs_platform_collect_emulators_from_dir(target_dir, codes, &count, capacity) != 0) {
            return -1;
        }
        if (snprintf(target_dir, sizeof(target_dir), "%s/%s/paks/Emus", paths->system_root, platform_name) < 0) {
            return -1;
        }
        if (cs_platform_collect_emulators_from_dir(target_dir, codes, &count, capacity) != 0) {
            return -1;
        }
    } else {
        if (cs_platform_collect_emulators_from_children(paths->emus_root, NULL, codes, &count, capacity) != 0) {
            return -1;
        }
        if (cs_platform_collect_emulators_from_children(paths->system_root, "paks/Emus", codes, &count, capacity) != 0) {
            return -1;
        }
    }

    if (count_out) {
        *count_out = count;
    }
    return 0;
}

int cs_platform_has_installed_emulator(const cs_platform_info *platform,
                                       const char codes[][CS_PLATFORM_CODE_MAX],
                                       size_t code_count) {
    size_t i;

    if (!platform || !codes) {
        return 0;
    }
    if (!cs_platform_requires_emulator(platform)) {
        return 1;
    }

    /* Emu resolution is tag-exact; MGBA/SUPA/SGB are separate paks in NextUI and report independently. */
    for (i = 0; i < code_count; ++i) {
        if (cs_platform_codes_equal(platform->tag, codes[i])) {
            return 1;
        }
    }

    return 0;
}
