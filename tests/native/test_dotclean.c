#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_dotclean.h"
#include "cs_paths.h"

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

static int has_path(const cs_dotclean_entry *entries, size_t count, const char *path) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (strcmp(entries[i].path, path) == 0) {
            return 1;
        }
    }

    return 0;
}

static void test_dotclean_finds_expected_entries_and_skips_large_trees(void) {
    char template[] = "/tmp/cs-dotclean-XXXXXX";
    char *root;
    char spotlight[PATH_MAX];
    char apdisk[PATH_MAX];
    char roms_dir[PATH_MAX];
    char nested_fsevents[PATH_MAX];
    char ds_store[PATH_MAX];
    char apple_double[PATH_MAX];
    char macosx_dir[PATH_MAX];
    char normal_file[PATH_MAX];
    char userdata_dir[PATH_MAX];
    char shared_dir[PATH_MAX];
    char minui_dir[PATH_MAX];
    char minui_ds_store[PATH_MAX];
    char bios_dir[PATH_MAX];
    char bios_apple_double[PATH_MAX];
    char deep_root[PATH_MAX];
    char deep_path[PATH_MAX];
    char deep_ds_store[PATH_MAX];
    cs_paths paths = {0};
    cs_dotclean_entry entries[CS_DOTCLEAN_MAX_ENTRIES];
    size_t count = 0;
    size_t count_only = 0;
    int truncated = 1;
    int count_only_truncated = 1;

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(spotlight, sizeof(spotlight), "%s/.Spotlight-V100", root) > 0);
    assert(snprintf(apdisk, sizeof(apdisk), "%s/.apdisk", root) > 0);
    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    assert(snprintf(nested_fsevents, sizeof(nested_fsevents), "%s/Roms/.fseventsd", root) > 0);
    assert(snprintf(ds_store, sizeof(ds_store), "%s/Roms/.DS_Store", root) > 0);
    assert(snprintf(apple_double, sizeof(apple_double), "%s/Roms/._Pokemon Emerald.gba", root) > 0);
    assert(snprintf(macosx_dir, sizeof(macosx_dir), "%s/Roms/__MACOSX", root) > 0);
    assert(snprintf(normal_file, sizeof(normal_file), "%s/Roms/Pokemon Emerald.gba", root) > 0);
    assert(snprintf(userdata_dir, sizeof(userdata_dir), "%s/.userdata", root) > 0);
    assert(snprintf(shared_dir, sizeof(shared_dir), "%s/.userdata/shared", root) > 0);
    assert(snprintf(minui_dir, sizeof(minui_dir), "%s/.userdata/shared/.minui", root) > 0);
    assert(snprintf(minui_ds_store, sizeof(minui_ds_store), "%s/.userdata/shared/.minui/.DS_Store", root) > 0);
    assert(snprintf(bios_dir, sizeof(bios_dir), "%s/Bios", root) > 0);
    assert(snprintf(bios_apple_double, sizeof(bios_apple_double), "%s/Bios/._gba_bios.bin", root) > 0);
    assert(snprintf(deep_root, sizeof(deep_root), "%s/Roms/deep", root) > 0);

    make_dir(spotlight);
    write_file(apdisk, "apdisk");
    make_dir(roms_dir);
    make_dir(nested_fsevents);
    make_dir(macosx_dir);
    make_dir(userdata_dir);
    make_dir(shared_dir);
    make_dir(minui_dir);
    make_dir(bios_dir);
    write_file(ds_store, "finder");
    write_file(apple_double, "appledouble");
    write_file(normal_file, "rom");
    write_file(minui_ds_store, "finder");
    write_file(bios_apple_double, "appledouble");

    assert(snprintf(deep_path, sizeof(deep_path), "%s", deep_root) > 0);
    make_dir(deep_path);
    for (size_t i = 0; i < CS_DOTCLEAN_MAX_DEPTH + 2; ++i) {
        char next_path[PATH_MAX];

        assert(snprintf(next_path, sizeof(next_path), "%s/level%02zu", deep_path, i) > 0);
        make_dir(next_path);
        assert(snprintf(deep_path, sizeof(deep_path), "%s", next_path) > 0);
    }
    assert(snprintf(deep_ds_store, sizeof(deep_ds_store), "%s/.DS_Store", deep_path) > 0);
    write_file(deep_ds_store, "finder");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_dotclean_scan(&paths, NULL, 0, &count_only, &count_only_truncated) == 0);
    assert(count_only == 5);
    assert(count_only_truncated == 0);
    assert(cs_dotclean_scan(&paths, entries, CS_DOTCLEAN_MAX_ENTRIES, &count, &truncated) == 0);

    assert(count == 5);
    assert(truncated == 0);
    assert(has_path(entries, count, ".Spotlight-V100") == 1);
    assert(has_path(entries, count, ".apdisk") == 1);
    assert(has_path(entries, count, "Roms/.DS_Store") == 1);
    assert(has_path(entries, count, "Roms/._Pokemon Emerald.gba") == 1);
    assert(has_path(entries, count, "Roms/__MACOSX") == 1);
    assert(has_path(entries, count, "Roms/Pokemon Emerald.gba") == 0);
    assert(has_path(entries, count, "Roms/.fseventsd") == 0);
    assert(has_path(entries, count, ".userdata/shared/.minui/.DS_Store") == 0);
    assert(has_path(entries, count, "Bios/._gba_bios.bin") == 0);
    assert(has_path(entries, count, "Roms/deep/level00/level01/level02/level03/level04/level05/level06/level07/level08/level09/level10/level11/level12/level13/level14/level15/level16/level17/level18/level19/level20/level21/level22/level23/level24/level25/level26/level27/level28/level29/level30/level31/level32/level33/.DS_Store")
           == 0);
}

static void test_dotclean_reports_truncation_without_losing_total_count(void) {
    char template[] = "/tmp/cs-dotclean-limit-XXXXXX";
    char *root;
    char roms_dir[PATH_MAX];
    cs_paths paths = {0};
    cs_dotclean_entry entries[CS_DOTCLEAN_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 0;
    size_t i;

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(roms_dir, sizeof(roms_dir), "%s/Roms", root) > 0);
    make_dir(roms_dir);

    for (i = 0; i < CS_DOTCLEAN_MAX_ENTRIES + 1; ++i) {
        char artifact_path[PATH_MAX];

        assert(snprintf(artifact_path, sizeof(artifact_path), "%s/._artifact%03zu", roms_dir, i) > 0);
        write_file(artifact_path, "appledouble");
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_dotclean_scan(&paths, entries, CS_DOTCLEAN_MAX_ENTRIES, &count, &truncated) == 0);

    assert(count == CS_DOTCLEAN_MAX_ENTRIES + 1);
    assert(truncated == 1);
    assert(entries[0].path[0] != '\0');
}

int main(void) {
    test_dotclean_finds_expected_entries_and_skips_large_trees();
    test_dotclean_reports_truncation_without_losing_total_count();
    return 0;
}
