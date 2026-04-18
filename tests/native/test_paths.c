#include <assert.h>
#include <stdlib.h>
#include <string.h>
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
