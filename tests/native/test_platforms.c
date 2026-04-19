#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_paths.h"
#include "cs_platforms.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path, const char *content);

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    assert(realpath(root, resolved) != NULL);
    assert(setenv("SDCARD_PATH", resolved, 1) == 0);
    unsetenv("CS_WEB_ROOT");
}

static const cs_platform_info *find_platform_entry(const cs_platform_info *platforms,
                                                   size_t count,
                                                   const char *tag) {
    size_t i;

    assert(platforms != NULL);
    assert(tag != NULL);

    for (i = 0; i < count; ++i) {
        if (strcmp(platforms[i].tag, tag) == 0) {
            return &platforms[i];
        }
    }

    return NULL;
}

static void assert_known_icon(const char *tag, const char *icon) {
    const cs_platform_info *info = cs_platform_find(tag);

    assert(info != NULL);
    assert(strcmp(info->icon, icon) == 0);
}

static void test_static_platform_metadata(void) {
    size_t count = cs_platform_count();
    const cs_platform_info *info;
    size_t i;

    assert(count > 0);

    info = cs_platform_at(0);
    assert(info != NULL);
    assert(info->tag[0] != '\0');
    assert(info->name[0] != '\0');
    assert(info->group[0] != '\0');
    assert(info->icon[0] != '\0');
    assert(info->primary_code[0] != '\0');
    assert(info->rom_directory[0] != '\0');

    assert(cs_platform_at(count) == NULL);
    assert(cs_platform_at(count + 100) == NULL);

    for (i = 0; i < count; ++i) {
        const cs_platform_info *entry = cs_platform_at(i);
        const cs_platform_info *round_trip;

        assert(entry != NULL);
        assert(entry->tag[0] != '\0');

        round_trip = cs_platform_find(entry->tag);
        assert(round_trip == entry);
    }

    assert(cs_platform_find(NULL) == NULL);
    assert(cs_platform_find("") == NULL);
    assert(cs_platform_find("DOES_NOT_EXIST") == NULL);
    assert(cs_platform_find("nes") == NULL);

    info = cs_platform_find("PS");
    assert(info != NULL);
    assert(strcmp(info->tag, "PS") == 0);
    assert(strcmp(info->group, "Sony") == 0);
    assert(strcmp(info->rom_directory, "Sony PlayStation (PS)") == 0);

    info = cs_platform_find("SNES");
    assert(info != NULL);
    assert(strcmp(info->primary_code, "SFC") == 0);
    assert(strcmp(info->rom_directory, "Super Nintendo Entertainment System (SFC)") == 0);

    info = cs_platform_find("NES");
    assert(info != NULL);
    assert(strcmp(info->primary_code, "FC") == 0);

    assert_known_icon("CPC", "CPC");
    assert_known_icon("C128", "C128");
    assert_known_icon("C64", "C64");
    assert_known_icon("MSX", "MSX");
    assert_known_icon("P8", "PICO8");
    assert_known_icon("VIC", "VIC20");
}

static void test_portmaster_platform_metadata(void) {
    const cs_platform_info *info = cs_platform_find("PORTS");

    assert(info != NULL);
    assert(strcmp(info->name, "Ports") == 0);
    assert(strcmp(info->group, "PortMaster") == 0);
    assert(strcmp(info->icon, "PORTMASTER") == 0);
    assert(strcmp(info->rom_directory, "Ports (PORTS)") == 0);
    assert(cs_platform_supports_resource(info, "roms") == 1);
    assert(cs_platform_supports_resource(info, "saves") == 0);
    assert(cs_platform_supports_resource(info, "states") == 0);
    assert(cs_platform_supports_resource(info, "bios") == 0);
    assert(cs_platform_supports_resource(info, "overlays") == 0);
    assert(cs_platform_supports_resource(info, "cheats") == 0);
    assert(cs_platform_allows_hidden_rom_entries(info) == 1);
}

static void test_parse_rejects_unsafe_custom_platform_codes(void) {
    char system_name[128];
    char system_code[32];

    assert(cs_platform_parse_rom_directory("Custom Platform (CUSTOM)", system_name, sizeof(system_name), system_code, sizeof(system_code))
           == 0);
    assert(strcmp(system_name, "Custom Platform") == 0);
    assert(strcmp(system_code, "CUSTOM") == 0);

    assert(cs_platform_parse_rom_directory("Unsafe (..)", system_name, sizeof(system_name), system_code, sizeof(system_code))
           == -1);
    assert(cs_platform_parse_rom_directory("Unsafe (.hidden)", system_name, sizeof(system_name), system_code, sizeof(system_code))
           == -1);
    assert(cs_platform_parse_rom_directory("Unsafe (BAD/CODE)", system_name, sizeof(system_name), system_code, sizeof(system_code))
           == -1);
}

static void test_alias_rom_directories_are_resolved(void) {
    char template[] = "/tmp/cs-platforms-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char nes_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info resolved = {0};
    cs_platform_info discovered[256];
    size_t discovered_count = 0;
    const cs_platform_info *fc;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(nes_dir, sizeof(nes_dir), "%s/Roms/Nintendo Entertainment System (NES)", root) > 0);

    make_dir(roms_dir);
    make_dir(nes_dir);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_platform_discover(&paths,
                                discovered,
                                sizeof(discovered) / sizeof(discovered[0]),
                                &discovered_count)
           == 0);
    fc = find_platform_entry(discovered, discovered_count, "FC");
    assert(fc != NULL);
    assert(strcmp(fc->rom_directory, "Nintendo Entertainment System (NES)") == 0);

    assert(cs_platform_resolve(&paths, "FC", &resolved) == 0);
    assert(strcmp(resolved.rom_directory, "Nintendo Entertainment System (NES)") == 0);
    assert(cs_platform_resolve(&paths, "NES", &resolved) == 0);
    assert(strcmp(resolved.rom_directory, "Nintendo Entertainment System (NES)") == 0);

    assert(rmdir(nes_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_shortcut_directories_are_excluded_from_discovery(void) {
    char template[] = "/tmp/cs-platforms-shortcuts-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char shortcut_dir[PATH_MAX];
    char shortcut_marker[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info discovered[256];
    cs_platform_info resolved = {0};
    size_t discovered_count = 0;
    const cs_platform_info *md;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(shortcut_dir, sizeof(shortcut_dir), "%s/Roms/0) Sonic - Spindash (MD)", root) > 0);
    assert(snprintf(shortcut_marker, sizeof(shortcut_marker), "%s/.shortcut", shortcut_dir) > 0);

    make_dir(roms_dir);
    make_dir(shortcut_dir);
    write_file(shortcut_marker, "Sonic - Spindash");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_discover(&paths,
                                discovered,
                                sizeof(discovered) / sizeof(discovered[0]),
                                &discovered_count)
           == 0);

    md = find_platform_entry(discovered, discovered_count, "MD");
    assert(md != NULL);
    assert(strcmp(md->rom_directory, "Sega Genesis (MD)") == 0);
    assert(cs_platform_resolve(&paths, "MD", &resolved) == 0);
    assert(strcmp(resolved.rom_directory, "Sega Genesis (MD)") == 0);
    assert(cs_platform_is_shortcut_directory("0) Sonic - Spindash (MD)", shortcut_dir) == 1);
    assert(cs_platform_is_shortcut_directory("\xE2\x98\x85 Old Shortcut (MD)", "/tmp/legacy-shortcut") == 1);

    assert(remove(shortcut_marker) == 0);
    assert(rmdir(shortcut_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void test_textures_directory_is_exposed_as_custom_platform(void) {
    char template[] = "/tmp/cs-platforms-custom-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char textures_dir[PATH_MAX];
    char dreamcast_dir[PATH_MAX];
    char textures_file[PATH_MAX];
    char dreamcast_file[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info discovered[256];
    size_t discovered_count = 0;
    const cs_platform_info *textures;
    const cs_platform_info *dreamcast;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(textures_dir, sizeof(textures_dir), "%s/Roms/Textures (GL)", root) > 0);
    assert(snprintf(dreamcast_dir, sizeof(dreamcast_dir), "%s/Roms/Dreamcast (FLYCAST)", root) > 0);
    assert(snprintf(textures_file, sizeof(textures_file), "%s/logo.png", textures_dir) > 0);
    assert(snprintf(dreamcast_file, sizeof(dreamcast_file), "%s/sonic.cdi", dreamcast_dir) > 0);

    make_dir(roms_dir);
    make_dir(textures_dir);
    make_dir(dreamcast_dir);
    write_file(textures_file, "png");
    write_file(dreamcast_file, "cdi");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_discover(&paths,
                                discovered,
                                sizeof(discovered) / sizeof(discovered[0]),
                                &discovered_count)
           == 0);

    textures = find_platform_entry(discovered, discovered_count, "GL");
    assert(textures != NULL);
    assert(textures->is_custom == 1);
    assert(strcmp(textures->name, "Textures") == 0);
    assert(strcmp(textures->rom_directory, "Textures (GL)") == 0);

    dreamcast = find_platform_entry(discovered, discovered_count, "FLYCAST");
    assert(dreamcast != NULL);
    assert(dreamcast->is_custom == 1);
    assert(strcmp(dreamcast->name, "Dreamcast") == 0);

    assert(remove(textures_file) == 0);
    assert(remove(dreamcast_file) == 0);
    assert(rmdir(textures_dir) == 0);
    assert(rmdir(dreamcast_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_custom_platform_icons_are_mapped(void) {
    char template[] = "/tmp/cs-platforms-icons-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char dir_path[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info resolved = {0};
    size_t i;
    const struct {
        const char *dir_name;
        const char *tag;
        const char *icon;
    } cases[] = {
        {"Atari 800 (A800)", "A800", "A800"},
        {"Dreamcast (FLYCAST)", "FLYCAST", "DC"},
        {"3DO (3DO)", "3DO", "3DO"},
        {"RPG Maker 2000-2003 (EASYRPG)", "EASYRPG", "RPGM"},
        {"Intellivision (INTV)", "INTV", "INTELLIVISION"},
        {"Atari Jaguar (JAGUAR)", "JAGUAR", "JAGUAR"},
        {"Nintendo 64 (P64)", "P64", "N64"},
        {"Nintendo DS (NDS)", "NDS", "NDS"},
        {"PICO-8 (PICO)", "PICO", "PICO8"},
        {"PlayStation Portable (PPSSPP)", "PPSSPP", "PSP"},
        {"Sega Saturn (SATURN)", "SATURN", "SATURN"},
        {"ScummVM (SCUMMVM)", "SCUMMVM", "SCUMMVM"},
        {"PC Engine SuperGrafx (SUPERGRAFX)", "SUPERGRAFX", "SUPERGRAFX"},
        {"TIC-80 (TIC)", "TIC", "TIC80"},
    };

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    make_dir(roms_dir);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(snprintf(dir_path, sizeof(dir_path), "%s/Roms/%s", root, cases[i].dir_name) > 0);
        make_dir(dir_path);
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(cs_platform_resolve(&paths, cases[i].tag, &resolved) == 0);
        assert(strcmp(resolved.icon, cases[i].icon) == 0);
    }

    for (i = sizeof(cases) / sizeof(cases[0]); i > 0; --i) {
        assert(snprintf(dir_path, sizeof(dir_path), "%s/Roms/%s", root, cases[i - 1].dir_name) > 0);
        assert(rmdir(dir_path) == 0);
    }
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    test_static_platform_metadata();
    test_portmaster_platform_metadata();
    test_parse_rejects_unsafe_custom_platform_codes();
    test_alias_rom_directories_are_resolved();
    test_shortcut_directories_are_excluded_from_discovery();
    test_textures_directory_is_exposed_as_custom_platform();
    test_custom_platform_icons_are_mapped();
    return 0;
}
