#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_paths.h"

static void assert_default_paths(const cs_paths *paths) {
    assert(strcmp(paths->sdcard_root, "/mnt/SDCARD") == 0);
    assert(strcmp(paths->shared_state_root, "/mnt/SDCARD/.userdata/shared/CentralScrutinizer") == 0);
    assert(strcmp(paths->web_root, "web/out") == 0);
    assert(strcmp(paths->roms_root, "/mnt/SDCARD/Roms") == 0);
    assert(strcmp(paths->saves_root, "/mnt/SDCARD/Saves") == 0);
    assert(strcmp(paths->bios_root, "/mnt/SDCARD/Bios") == 0);
    assert(strcmp(paths->temp_upload_root, "/mnt/SDCARD/.userdata/shared/CentralScrutinizer/uploads/tmp") == 0);
}

static void assert_fixture_paths(const cs_paths *paths) {
    assert(strcmp(paths->sdcard_root, "fixtures/mock_sdcard") == 0);
    assert(strcmp(paths->shared_state_root, "fixtures/mock_sdcard/.userdata/shared/CentralScrutinizer") == 0);
    assert(strcmp(paths->web_root, "custom/web/root") == 0);
    assert(strcmp(paths->roms_root, "fixtures/mock_sdcard/Roms") == 0);
    assert(strcmp(paths->saves_root, "fixtures/mock_sdcard/Saves") == 0);
    assert(strcmp(paths->bios_root, "fixtures/mock_sdcard/Bios") == 0);
    assert(strcmp(paths->temp_upload_root, "fixtures/mock_sdcard/.userdata/shared/CentralScrutinizer/uploads/tmp") == 0);
}

static void fill_sentinel(cs_paths *paths) {
    memset(paths, 0xA5, sizeof(*paths));
}

static void assert_unchanged(const cs_paths *actual, const cs_paths *expected) {
    assert(memcmp(actual, expected, sizeof(*actual)) == 0);
}

static void assert_fixture_file(const char *path) {
    assert(access(path, F_OK) == 0);
}

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void test_absolute_sdcard_path_is_canonicalized(void) {
    cs_paths paths;
    char template[] = "/tmp/cs-paths-XXXXXX";
    char *root;
    char actual_sdcard[PATH_MAX];
    char link_sdcard[PATH_MAX];
    char expected_root[PATH_MAX];

    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(actual_sdcard, sizeof(actual_sdcard), "%s/sdcard-real", root) > 0);
    assert(snprintf(link_sdcard, sizeof(link_sdcard), "%s/SDCARD", root) > 0);

    make_dir(actual_sdcard);
    assert(symlink(actual_sdcard, link_sdcard) == 0);
    assert(realpath(actual_sdcard, expected_root) != NULL);

    setenv("SDCARD_PATH", link_sdcard, 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, expected_root) == 0);

    assert(unlink(link_sdcard) == 0);
    assert(rmdir(actual_sdcard) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    cs_paths paths;
    cs_paths expected;
    char oversized[CS_PATH_MAX * 2];
    char boundary_sdcard[CS_PATH_MAX];

    unsetenv("SDCARD_PATH");
    unsetenv("CS_WEB_ROOT");

    assert(cs_paths_init(NULL) == -1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_default_paths(&paths);

    setenv("SDCARD_PATH", "/mnt/sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, "/mnt/sdcard") == 0);
    assert(strcmp(paths.shared_state_root, "/mnt/sdcard/.userdata/shared/CentralScrutinizer") == 0);

    setenv("SDCARD_PATH", "/definitely/missing/sdcard", 1);
    unsetenv("CS_WEB_ROOT");

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert(strcmp(paths.sdcard_root, "/definitely/missing/sdcard") == 0);

    test_absolute_sdcard_path_is_canonicalized();
    assert_fixture_file("fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)");
    assert_fixture_file("fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)/.media/Pokemon Emerald.png");
    assert_fixture_file("fixtures/mock_sdcard/Roms/PlayStation (PS)/Castlevania - Symphony of the Night.chd");
    assert_fixture_file("fixtures/mock_sdcard/Bios/PS/scph1001.bin");
    assert_fixture_file("fixtures/mock_sdcard/.userdata/shared/CentralScrutinizer/.keep");

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_fixture_paths(&paths);

    setenv("SDCARD_PATH", "", 1);
    setenv("CS_WEB_ROOT", "", 1);

    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    assert_default_paths(&paths);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    setenv("SDCARD_PATH", oversized, 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(oversized, 'y', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", oversized, 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);
    fill_sentinel(&paths);
    assert(cs_paths_init(&paths) == 0);
    expected = paths;

    memset(boundary_sdcard, 'z', sizeof(boundary_sdcard) - 1);
    boundary_sdcard[sizeof(boundary_sdcard) - 1] = '\0';
    setenv("SDCARD_PATH", boundary_sdcard, 1);
    setenv("CS_WEB_ROOT", "custom/web/root", 1);

    assert(cs_paths_init(&paths) == -1);
    assert_unchanged(&paths, &expected);

    return 0;
}
