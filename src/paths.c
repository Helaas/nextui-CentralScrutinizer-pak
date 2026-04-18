#include "cs_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_value(char *dst, size_t size, const char *value, const char *fallback) {
    const char *source = (value != NULL && value[0] != '\0') ? value : fallback;
    int written = snprintf(dst, size, "%s", source);

    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

static int write_sdcard_root(char *dst, size_t size, const char *value, const char *fallback) {
    const char *source = (value != NULL && value[0] != '\0') ? value : fallback;

    if (!dst || size == 0 || !source) {
        return -1;
    }

    if (source[0] == '/') {
        char resolved[CS_PATH_MAX];

        if (realpath(source, resolved) != NULL) {
            source = resolved;
        }
    }

    return write_value(dst, size, source, fallback);
}

static int write_joined(char *dst, size_t size, const char *prefix, const char *suffix) {
    int written = snprintf(dst, size, "%s%s", prefix, suffix);

    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

int cs_paths_init(cs_paths *paths) {
    const char *sd;
    const char *web;
    const char *default_web_root = "web/out";
    cs_paths temp = {0};

    if (!paths) {
        return -1;
    }

    sd = getenv("SDCARD_PATH");
    web = getenv("CS_WEB_ROOT");

    if ((!web || web[0] == '\0') && access("resources/web", R_OK | X_OK) == 0) {
        default_web_root = "resources/web";
    }

    if (write_sdcard_root(temp.sdcard_root, sizeof(temp.sdcard_root), sd, "/mnt/SDCARD") != 0) {
        return -1;
    }
    if (write_joined(temp.shared_state_root, sizeof(temp.shared_state_root), temp.sdcard_root, "/.userdata/shared/CentralScrutinizer") != 0) {
        return -1;
    }
    if (write_joined(temp.roms_root, sizeof(temp.roms_root), temp.sdcard_root, "/Roms") != 0) {
        return -1;
    }
    if (write_joined(temp.saves_root, sizeof(temp.saves_root), temp.sdcard_root, "/Saves") != 0) {
        return -1;
    }
    if (write_joined(temp.bios_root, sizeof(temp.bios_root), temp.sdcard_root, "/Bios") != 0) {
        return -1;
    }
    if (write_joined(temp.overlays_root, sizeof(temp.overlays_root), temp.sdcard_root, "/Overlays") != 0) {
        return -1;
    }
    if (write_joined(temp.cheats_root, sizeof(temp.cheats_root), temp.sdcard_root, "/Cheats") != 0) {
        return -1;
    }
    if (write_joined(temp.collections_root, sizeof(temp.collections_root), temp.sdcard_root, "/Collections") != 0) {
        return -1;
    }
    if (write_joined(temp.screenshots_root, sizeof(temp.screenshots_root), temp.sdcard_root, "/Screenshots") != 0) {
        return -1;
    }
    if (write_joined(temp.temp_upload_root, sizeof(temp.temp_upload_root), temp.shared_state_root, "/uploads/tmp") != 0) {
        return -1;
    }
    if (write_value(temp.web_root, sizeof(temp.web_root), web, default_web_root) != 0) {
        return -1;
    }

    *paths = temp;
    return 0;
}
