#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cs_settings.h"

static void fill_paths(cs_paths *paths, const char *root) {
    memset(paths, 0, sizeof(*paths));
    snprintf(paths->shared_state_root, sizeof(paths->shared_state_root), "%s/.userdata/shared/CentralScrutinizer", root);
}

static void write_file(const char *path, const char *body) {
    FILE *fp = fopen(path, "wb");

    assert(fp != NULL);
    assert(fputs(body, fp) != EOF);
    assert(fclose(fp) == 0);
}

int main(void) {
    char template[] = "/tmp/cs-settings-XXXXXX";
    char path[CS_PATH_MAX];
    cs_paths paths;
    cs_settings settings = {0};
    cs_settings loaded = {0};
    char *temp_root = mkdtemp(template);

    assert(temp_root != NULL);

    fill_paths(&paths, temp_root);

#if defined(PLATFORM_MAC)
    assert(cs_settings_default_terminal_enabled() == 1);
#else
    assert(cs_settings_default_terminal_enabled() == 0);
#endif

    assert(cs_settings_make_path(&paths, path, sizeof(path)) == 0);
    assert(strstr(path, "/settings.json") != NULL);

    loaded.terminal_enabled = 99;
    loaded.keep_awake_in_background = 99;
    assert(cs_settings_load(&paths, &loaded) == 0);
    assert(loaded.terminal_enabled == cs_settings_default_terminal_enabled());
    assert(loaded.keep_awake_in_background == cs_settings_default_keep_awake_in_background());

    settings.terminal_enabled = 0;
    settings.keep_awake_in_background = 1;
    assert(cs_settings_save(&paths, &settings) == 0);

    loaded.terminal_enabled = 1;
    loaded.keep_awake_in_background = 0;
    assert(cs_settings_load(&paths, &loaded) == 0);
    assert(loaded.terminal_enabled == 0);
    assert(loaded.keep_awake_in_background == 1);

    write_file(path, "{not-json");
    loaded.terminal_enabled = 77;
    loaded.keep_awake_in_background = 77;
    assert(cs_settings_load(&paths, &loaded) == 0);
    assert(loaded.terminal_enabled == cs_settings_default_terminal_enabled());
    assert(loaded.keep_awake_in_background == cs_settings_default_keep_awake_in_background());

    write_file(path, "{\"terminal_enabled\":true}");
    loaded.terminal_enabled = 0;
    loaded.keep_awake_in_background = 1;
    assert(cs_settings_load(&paths, &loaded) == 0);
    assert(loaded.terminal_enabled == 1);
    assert(loaded.keep_awake_in_background == 0);

    unlink(path);
    rmdir(paths.shared_state_root);
    return 0;
}
