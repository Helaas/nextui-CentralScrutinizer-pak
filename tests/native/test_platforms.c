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
    assert(cs_platform_requires_emulator(info) == 0);
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

static void test_custom_rom_directories_are_not_exposed_in_library(void) {
    char template[] = "/tmp/cs-platforms-custom-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char textures_dir[PATH_MAX];
    char dreamcast_dir[PATH_MAX];
    char textures_file[PATH_MAX];
    char dreamcast_file[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info discovered[256];
    cs_platform_info resolved = {0};
    size_t discovered_count = 0;

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

    assert(find_platform_entry(discovered, discovered_count, "GL") == NULL);
    assert(find_platform_entry(discovered, discovered_count, "FLYCAST") == NULL);
    assert(cs_platform_resolve(&paths, "GL", &resolved) == -1);
    assert(cs_platform_resolve(&paths, "FLYCAST", &resolved) == -1);

    assert(remove(textures_file) == 0);
    assert(remove(dreamcast_file) == 0);
    assert(rmdir(textures_dir) == 0);
    assert(rmdir(dreamcast_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_ports_are_only_discovered_when_installed(void) {
    char template[] = "/tmp/cs-platforms-ports-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char ports_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_platform_info discovered[256];
    cs_platform_info resolved = {0};
    size_t discovered_count = 0;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(ports_dir, sizeof(ports_dir), "%s/Roms/Ports (PORTS)", root) > 0);
    make_dir(roms_dir);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_discover(&paths,
                                discovered,
                                sizeof(discovered) / sizeof(discovered[0]),
                                &discovered_count)
           == 0);
    assert(find_platform_entry(discovered, discovered_count, "PORTS") == NULL);
    assert(cs_platform_resolve(&paths, "PORTS", &resolved) == -1);

    make_dir(ports_dir);

    assert(cs_platform_discover(&paths,
                                discovered,
                                sizeof(discovered) / sizeof(discovered[0]),
                                &discovered_count)
           == 0);
    assert(find_platform_entry(discovered, discovered_count, "PORTS") != NULL);
    assert(cs_platform_resolve(&paths, "PORTS", &resolved) == 0);

    assert(rmdir(ports_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_emulator_scan_checks_emus_and_system_paks(void) {
    char template[] = "/tmp/cs-platforms-emus-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char emus_root[PATH_MAX];
    char emus_platform_dir[PATH_MAX];
    char system_root[PATH_MAX];
    char system_platform_dir[PATH_MAX];
    char system_paks_dir[PATH_MAX];
    char system_emus_dir[PATH_MAX];
    char gba_pak[PATH_MAX];
    char fc_pak[PATH_MAX];
    cs_paths paths = {0};
    char codes[16][CS_PLATFORM_CODE_MAX];
    size_t code_count = 0;
    const cs_platform_info *gba = cs_platform_find("GBA");
    const cs_platform_info *fc = cs_platform_find("FC");
    const cs_platform_info *ports = cs_platform_find("PORTS");

    assert(gba != NULL);
    assert(fc != NULL);
    assert(ports != NULL);

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(emus_root, sizeof(emus_root), "%s/Emus", root) > 0);
    assert(snprintf(emus_platform_dir, sizeof(emus_platform_dir), "%s/Emus/tg5040", root) > 0);
    assert(snprintf(system_root, sizeof(system_root), "%s/.system", root) > 0);
    assert(snprintf(system_platform_dir, sizeof(system_platform_dir), "%s/.system/tg5050", root) > 0);
    assert(snprintf(system_paks_dir, sizeof(system_paks_dir), "%s/.system/tg5050/paks", root) > 0);
    assert(snprintf(system_emus_dir, sizeof(system_emus_dir), "%s/.system/tg5050/paks/Emus", root) > 0);
    assert(snprintf(gba_pak, sizeof(gba_pak), "%s/GBA.pak", emus_platform_dir) > 0);
    assert(snprintf(fc_pak, sizeof(fc_pak), "%s/NES.pak", system_emus_dir) > 0);

    make_dir(roms_dir);
    make_dir(emus_root);
    make_dir(emus_platform_dir);
    make_dir(system_root);
    make_dir(system_platform_dir);
    make_dir(system_paks_dir);
    make_dir(system_emus_dir);
    make_dir(gba_pak);
    make_dir(fc_pak);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_platform_collect_installed_emulators(&paths, codes, sizeof(codes) / sizeof(codes[0]), &code_count) == 0);
    assert(code_count == 2);
    assert(cs_platform_has_installed_emulator(gba, (const char (*)[CS_PLATFORM_CODE_MAX]) codes, code_count) == 1);
    assert(cs_platform_has_installed_emulator(fc, (const char (*)[CS_PLATFORM_CODE_MAX]) codes, code_count) == 1);
    assert(cs_platform_has_installed_emulator(ports, (const char (*)[CS_PLATFORM_CODE_MAX]) codes, code_count) == 1);

    assert(rmdir(gba_pak) == 0);
    assert(rmdir(fc_pak) == 0);
    assert(rmdir(system_emus_dir) == 0);
    assert(rmdir(system_paks_dir) == 0);
    assert(rmdir(system_platform_dir) == 0);
    assert(rmdir(system_root) == 0);
    assert(rmdir(emus_platform_dir) == 0);
    assert(rmdir(emus_root) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    test_static_platform_metadata();
    test_portmaster_platform_metadata();
    test_parse_rejects_unsafe_custom_platform_codes();
    test_alias_rom_directories_are_resolved();
    test_shortcut_directories_are_excluded_from_discovery();
    test_custom_rom_directories_are_not_exposed_in_library();
    test_ports_are_only_discovered_when_installed();
    test_emulator_scan_checks_emus_and_system_paks();
    return 0;
}
