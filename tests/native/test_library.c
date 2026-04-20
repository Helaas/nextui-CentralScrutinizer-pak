#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_library.h"
#include "cs_paths.h"
#include "cs_platforms.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    assert(realpath(root, resolved) != NULL);
    setenv("SDCARD_PATH", resolved, 1);
    unsetenv("CS_WEB_ROOT");
}

static const cs_browser_entry *find_entry(const cs_browser_result *result, const char *name) {
    size_t i;

    assert(result != NULL);
    assert(name != NULL);
    for (i = 0; i < result->count; ++i) {
        if (strcmp(result->entries[i].name, name) == 0) {
            return &result->entries[i];
        }
    }

    return NULL;
}

static const cs_browser_entry *find_entry_by_type(const cs_browser_result *result, const char *type) {
    size_t i;

    assert(result != NULL);
    assert(type != NULL);
    for (i = 0; i < result->count; ++i) {
        if (strcmp(result->entries[i].type, type) == 0) {
            return &result->entries[i];
        }
    }

    return NULL;
}

static void test_fixture_browser_scopes_and_rejection(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    const cs_platform_info *gba;
    const cs_platform_info *ps;
    const cs_browser_entry *entry;

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    assert(cs_paths_init(&paths) == 0);
    gba = cs_platform_find("GBA");
    ps = cs_platform_find("PS");
    assert(gba != NULL);
    assert(ps != NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == 0);
    assert(strcmp(result.scope, "roms") == 0);
    assert(strcmp(result.title, "ROMs - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)") == 0);
    assert(result.count == 2);
    assert(result.truncated == 0);
    entry = find_entry(&result, ".media");
    assert(entry != NULL);
    assert(strcmp(entry->type, "directory") == 0);
    entry = find_entry_by_type(&result, "rom");
    assert(entry != NULL);
    assert(strcmp(entry->type, "rom") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_SAVES, gba, "", &result) == 0);
    assert(strcmp(result.title, "Saves - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Saves/GBA") == 0);
    assert(result.count == 1);
    entry = find_entry(&result, "Pokemon Emerald.sav");
    assert(entry != NULL);
    assert(strcmp(entry->type, "save") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_BIOS, ps, "", &result) == 0);
    assert(strcmp(result.title, "BIOS - Sony PlayStation") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Bios/PS") == 0);
    assert(result.count == 1);
    entry = find_entry(&result, "scph1001.bin");
    assert(entry != NULL);
    assert(strcmp(entry->type, "bios") == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_OVERLAYS, gba, "", &result) == 0);
    assert(strcmp(result.scope, "overlays") == 0);
    assert(strcmp(result.title, "Overlays - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Overlays/GBA") == 0);
    assert(result.count == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_CHEATS, gba, "", &result) == 0);
    assert(strcmp(result.scope, "cheats") == 0);
    assert(strcmp(result.title, "Cheats - Game Boy Advance") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard/Cheats/GBA") == 0);
    assert(result.count == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "", &result) == 0);
    assert(strcmp(result.title, "File Browser") == 0);
    assert(strcmp(result.root_path, "fixtures/mock_sdcard") == 0);
    assert(find_entry(&result, ".userdata") != NULL);
    assert(find_entry(&result, "Bios") != NULL);
    assert(find_entry(&result, "Roms") != NULL);
    assert(find_entry(&result, "Saves") != NULL);

    assert(cs_browser_list(&paths,
                           CS_SCOPE_FILES,
                           NULL,
                           ".userdata/shared/CentralScrutinizer",
                           &result)
           == 0);
    assert(result.breadcrumb_count == 3);
    assert(strcmp(result.breadcrumbs[0].label, ".userdata") == 0);
    assert(strcmp(result.breadcrumbs[1].label, "shared") == 0);
    assert(strcmp(result.breadcrumbs[2].label, "CentralScrutinizer") == 0);
    assert(find_entry(&result, ".keep") != NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, NULL, "", &result) == -1);
    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "../outside", &result) == -1);
}

static void test_rom_thumbnail_resolution_is_png_only(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-thumb-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char media_dir[PATH_MAX];
    char rom_file[PATH_MAX];
    char png_art[PATH_MAX];
    char jpg_art[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");
    const cs_browser_entry *entry;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/Game Boy Advance (GBA)", root) > 0);
    assert(snprintf(media_dir, sizeof(media_dir), "%s/Roms/Game Boy Advance (GBA)/.media", root) > 0);
    assert(snprintf(rom_file, sizeof(rom_file), "%s/Box Art Test.gba", system_dir) > 0);
    assert(snprintf(png_art, sizeof(png_art), "%s/Box Art Test.png", media_dir) > 0);
    assert(snprintf(jpg_art, sizeof(jpg_art), "%s/Box Art Test.jpg", media_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    make_dir(media_dir);
    write_file(rom_file, "rom");
    write_file(png_art, "png");
    write_file(jpg_art, "jpg");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == 0);
    entry = find_entry(&result, "Box Art Test.gba");
    assert(entry != NULL);
    assert(strcmp(entry->thumbnail_path, ".media/Box Art Test.png") == 0);

    assert(unlink(png_art) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == 0);
    entry = find_entry(&result, "Box Art Test.gba");
    assert(entry != NULL);
    assert(entry->thumbnail_path[0] == '\0');

    assert(unlink(jpg_art) == 0);
    assert(unlink(rom_file) == 0);
    assert(rmdir(media_dir) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlink_entries_are_skipped(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char real_rom[PATH_MAX];
    char outside_file[PATH_MAX];
    char link_path[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/Game Boy Advance (GBA)", root) > 0);
    assert(snprintf(real_rom, sizeof(real_rom), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(outside_file, sizeof(outside_file), "%s/not-a-rom.bin", root) > 0);
    assert(snprintf(link_path, sizeof(link_path), "%s/Outside Link.gba", system_dir) > 0);

    make_dir(roms_dir);
    make_dir(system_dir);
    write_file(real_rom, "rom");
    write_file(outside_file, "outside");
    assert(symlink(outside_file, link_path) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == 0);
    assert(result.count == 1);
    assert(result.truncated == 0);
    assert(strcmp(result.entries[0].name, "Pokemon Emerald.gba") == 0);

    assert(unlink(link_path) == 0);
    assert(unlink(real_rom) == 0);
    assert(unlink(outside_file) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_scope_root_is_rejected(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-rootlink-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char outside_dir[PATH_MAX];
    char real_rom[PATH_MAX];
    char system_link[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(outside_dir, sizeof(outside_dir), "%s/outside-system", root) > 0);
    assert(snprintf(real_rom, sizeof(real_rom), "%s/Pokemon Emerald.gba", outside_dir) > 0);
    assert(snprintf(system_link, sizeof(system_link), "%s/Roms/Game Boy Advance (GBA)", root) > 0);

    make_dir(roms_dir);
    make_dir(outside_dir);
    write_file(real_rom, "rom");
    assert(symlink(outside_dir, system_link) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == -1);

    assert(unlink(system_link) == 0);
    assert(unlink(real_rom) == 0);
    assert(rmdir(outside_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_absolute_sdcard_root_is_canonicalized_for_files_scope(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-sdlink-XXXXXX";
    char *root;
    char actual_sdcard[PATH_MAX];
    char linked_sdcard[PATH_MAX];
    char roms_dir[PATH_MAX];
    char bios_dir[PATH_MAX];
    char saves_dir[PATH_MAX];
    char expected_root[PATH_MAX];

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(actual_sdcard, sizeof(actual_sdcard), "%s/sdcard-real", root) > 0);
    assert(snprintf(linked_sdcard, sizeof(linked_sdcard), "%s/SDCARD", root) > 0);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", actual_sdcard) > 0);
    assert(snprintf(bios_dir, sizeof(bios_dir), "%s/Bios", actual_sdcard) > 0);
    assert(snprintf(saves_dir, sizeof(saves_dir), "%s/Saves", actual_sdcard) > 0);

    make_dir(actual_sdcard);
    make_dir(roms_dir);
    make_dir(bios_dir);
    make_dir(saves_dir);
    assert(symlink(actual_sdcard, linked_sdcard) == 0);
    assert(realpath(actual_sdcard, expected_root) != NULL);

    setenv("SDCARD_PATH", linked_sdcard, 1);
    unsetenv("CS_WEB_ROOT");

    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, expected_root) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_FILES, NULL, "", &result) == 0);
    assert(strcmp(result.root_path, expected_root) == 0);
    assert(find_entry(&result, "Roms") != NULL);
    assert(find_entry(&result, "Bios") != NULL);
    assert(find_entry(&result, "Saves") != NULL);

    assert(unlink(linked_sdcard) == 0);
    assert(rmdir(saves_dir) == 0);
    assert(rmdir(bios_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(actual_sdcard) == 0);
    assert(rmdir(root) == 0);
}

static void test_symlinked_roms_parent_is_rejected(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-parentlink-XXXXXX";
    char outside_template[] = "/tmp/cs-library-parentoutside-XXXXXX";
    char *root;
    char *outside_root;
    char real_roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    char rom_file[PATH_MAX];
    char roms_link[PATH_MAX];
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    outside_root = mkdtemp(outside_template);
    assert(root != NULL);
    assert(outside_root != NULL);

    assert(snprintf(real_roms_dir, sizeof(real_roms_dir), "%s/real-roms", outside_root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/real-roms/Game Boy Advance (GBA)", outside_root) > 0);
    assert(snprintf(rom_file, sizeof(rom_file), "%s/Pokemon Emerald.gba", system_dir) > 0);
    assert(snprintf(roms_link, sizeof(roms_link), "%s/Roms", root) > 0);

    make_dir(real_roms_dir);
    make_dir(system_dir);
    write_file(rom_file, "rom");
    assert(symlink(real_roms_dir, roms_link) == 0);

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == -1);

    assert(unlink(roms_link) == 0);
    assert(unlink(rom_file) == 0);
    assert(rmdir(system_dir) == 0);
    assert(rmdir(real_roms_dir) == 0);
    assert(rmdir(outside_root) == 0);
    assert(rmdir(root) == 0);
}

static void test_large_listing_sets_truncated_flag(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-large-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char system_dir[PATH_MAX];
    int i;
    const cs_platform_info *gba = cs_platform_find("GBA");

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(system_dir, sizeof(system_dir), "%s/Roms/Game Boy Advance (GBA)", root) > 0);
    make_dir(roms_dir);
    make_dir(system_dir);

    for (i = 0; i < (int) CS_BROWSER_MAX_ENTRIES + 20; ++i) {
        char file_path[PATH_MAX];

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03d.gba", system_dir, i) > 0);
        write_file(file_path, "rom");
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, gba, "", &result) == 0);
    assert(result.count == CS_BROWSER_MAX_ENTRIES);
    assert(result.truncated == 1);

    for (i = 0; i < (int) CS_BROWSER_MAX_ENTRIES + 20; ++i) {
        char file_path[PATH_MAX];

        assert(snprintf(file_path, sizeof(file_path), "%s/Game %03d.gba", system_dir, i) > 0);
        assert(unlink(file_path) == 0);
    }
    assert(rmdir(system_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

static void test_ports_browser_supports_hidden_ports_and_rejects_other_resources(void) {
    cs_paths paths = {0};
    cs_browser_result result = {0};
    char template[] = "/tmp/cs-library-ports-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    char ports_dir[PATH_MAX];
    char hidden_ports_dir[PATH_MAX];
    char shortcut_dir[PATH_MAX];
    char shortcut_marker[PATH_MAX];
    char root_script[PATH_MAX];
    char port_manifest[PATH_MAX];
    const cs_platform_info *ports = cs_platform_find("PORTS");

    assert(ports != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(ports_dir, sizeof(ports_dir), "%s/Roms/Ports (PORTS)", root) > 0);
    assert(snprintf(hidden_ports_dir, sizeof(hidden_ports_dir), "%s/Roms/Ports (PORTS)/.ports", root) > 0);
    assert(snprintf(shortcut_dir, sizeof(shortcut_dir), "%s/Roms/Ports (PORTS)/0) Search (SHORTCUT)", root) > 0);
    assert(snprintf(shortcut_marker, sizeof(shortcut_marker), "%s/.shortcut", shortcut_dir) > 0);
    assert(snprintf(root_script, sizeof(root_script), "%s/PokeMMO.sh", ports_dir) > 0);
    assert(snprintf(port_manifest, sizeof(port_manifest), "%s/port.json", hidden_ports_dir) > 0);

    make_dir(roms_dir);
    make_dir(ports_dir);
    make_dir(hidden_ports_dir);
    make_dir(shortcut_dir);
    write_file(shortcut_marker, "Search");
    write_file(root_script, "launch");
    write_file(port_manifest, "{}");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, ports, "", &result) == 0);
    assert(find_entry(&result, ".ports") != NULL);
    assert(find_entry(&result, "PokeMMO.sh") != NULL);
    assert(find_entry(&result, "0) Search (SHORTCUT)") == NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_ROMS, ports, ".ports", &result) == 0);
    assert(strcmp(result.path, ".ports") == 0);
    assert(find_entry(&result, "port.json") != NULL);

    assert(cs_browser_list(&paths, CS_SCOPE_SAVES, ports, "", &result) == -1);
    assert(cs_browser_list(&paths, CS_SCOPE_BIOS, ports, "", &result) == -1);
    assert(cs_browser_list(&paths, CS_SCOPE_OVERLAYS, ports, "", &result) == -1);
    assert(cs_browser_list(&paths, CS_SCOPE_CHEATS, ports, "", &result) == -1);

    assert(remove(port_manifest) == 0);
    assert(remove(root_script) == 0);
    assert(remove(shortcut_marker) == 0);
    assert(rmdir(shortcut_dir) == 0);
    assert(rmdir(hidden_ports_dir) == 0);
    assert(rmdir(ports_dir) == 0);
    assert(rmdir(roms_dir) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    test_fixture_browser_scopes_and_rejection();
    test_rom_thumbnail_resolution_is_png_only();
    test_symlink_entries_are_skipped();
    test_symlinked_scope_root_is_rejected();
    test_symlinked_absolute_sdcard_root_is_canonicalized_for_files_scope();
    test_symlinked_roms_parent_is_rejected();
    test_large_listing_sets_truncated_flag();
    test_ports_browser_supports_hidden_ports_and_rejects_other_resources();
    return 0;
}
