#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cs_keep_awake.h"
#include "cs_paths.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0775) == 0 || errno == EEXIST);
}

static void make_dir_p(const char *path) {
    char buffer[CS_PATH_MAX];
    size_t i;

    assert(path != NULL);
    assert(strlen(path) < sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", path);

    for (i = 1; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '/') {
            continue;
        }
        buffer[i] = '\0';
        make_dir(buffer);
        buffer[i] = '/';
    }

    make_dir(buffer);
}

static void remove_tree(const char *path) {
    DIR *dir;
    struct dirent *entry;

    assert(path != NULL);

    dir = opendir(path);
    assert(dir != NULL);

    while ((entry = readdir(dir)) != NULL) {
        char child_path[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        assert(snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name) < (int) sizeof(child_path));
        assert(lstat(child_path, &st) == 0);
        if (S_ISDIR(st.st_mode)) {
            remove_tree(child_path);
            continue;
        }
        assert(unlink(child_path) == 0);
    }

    assert(closedir(dir) == 0);
    assert(rmdir(path) == 0);
}

static void make_settings_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    assert(paths != NULL);
    assert(buffer != NULL);
    assert(snprintf(buffer, buffer_len, "%s/minuisettings.txt", paths->shared_userdata_root) < (int) buffer_len);
}

static void make_keep_awake_state_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    assert(paths != NULL);
    assert(buffer != NULL);
    assert(snprintf(buffer, buffer_len, "%s/nextui-keep-awake-state.txt", paths->shared_state_root) < (int) buffer_len);
}

static void write_text_file(const char *path, const char *contents) {
    FILE *fp;
    char parent[CS_PATH_MAX];
    size_t i;

    assert(path != NULL);
    assert(contents != NULL);
    assert(strlen(path) < sizeof(parent));
    snprintf(parent, sizeof(parent), "%s", path);

    for (i = strlen(parent); i > 0; --i) {
        if (parent[i - 1] == '/') {
            parent[i - 1] = '\0';
            break;
        }
    }
    assert(i > 0);
    make_dir_p(parent);

    fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fwrite(contents, 1, strlen(contents), fp) == strlen(contents));
    assert(fclose(fp) == 0);
}

static char *read_text_file(const char *path) {
    FILE *fp;
    long size;
    char *contents;

    assert(path != NULL);

    fp = fopen(path, "rb");
    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    size = ftell(fp);
    assert(size >= 0);
    assert(fseek(fp, 0, SEEK_SET) == 0);

    contents = (char *) malloc((size_t) size + 1u);
    assert(contents != NULL);
    assert(size == 0 || fread(contents, 1, (size_t) size, fp) == (size_t) size);
    contents[size] = '\0';
    assert(fclose(fp) == 0);
    return contents;
}

static void assert_file_equals(const char *path, const char *expected) {
    char *contents;

    assert(path != NULL);
    assert(expected != NULL);
    contents = read_text_file(path);
    assert(strcmp(contents, expected) == 0);
    free(contents);
}

static void assert_file_contains(const char *path, const char *needle) {
    char *contents;

    assert(path != NULL);
    assert(needle != NULL);
    contents = read_text_file(path);
    assert(strstr(contents, needle) != NULL);
    free(contents);
}

static void assert_file_absent(const char *path) {
    assert(path != NULL);
    assert(access(path, F_OK) != 0);
}

int main(void) {
    char template[] = "/tmp/cs-keep-awake-XXXXXX";
    char *sdcard_root;
    char settings_path[CS_PATH_MAX];
    char keep_awake_state_path[CS_PATH_MAX];
    cs_paths paths;

    sdcard_root = mkdtemp(template);
    assert(sdcard_root != NULL);

    assert(setenv("SDCARD_PATH", sdcard_root, 1) == 0);
    assert(setenv("CS_PLATFORM_NAME_OVERRIDE", "my355", 1) == 0);
    assert(cs_paths_init(&paths) == 0);
    make_settings_path(&paths, settings_path, sizeof(settings_path));
    make_keep_awake_state_path(&paths, keep_awake_state_path, sizeof(keep_awake_state_path));

    write_text_file(settings_path, "volume=7\nscreentimeout=60\nbrightness=9\n");
    assert(cs_keep_awake_enable(&paths) == 0);
    assert(cs_keep_awake_enable(&paths) == 0);
    assert_file_contains(settings_path, "screentimeout=0\n");
    assert_file_contains(settings_path, "centralscrutinizer_keepawake=1\n");
    assert_file_contains(keep_awake_state_path, "original_screentimeout=screentimeout=60\n");
    assert(cs_keep_awake_disable(&paths) == 0);
    assert_file_equals(settings_path, "volume=7\nscreentimeout=60\nbrightness=9\n");
    assert_file_absent(keep_awake_state_path);

    write_text_file(settings_path, "volume=7\nbrightness=9\n");
    assert(cs_keep_awake_enable(&paths) == 0);
    assert_file_contains(settings_path, "volume=7\n");
    assert_file_contains(settings_path, "brightness=9\n");
    assert_file_contains(settings_path, "screentimeout=0\n");
    assert_file_contains(settings_path, "centralscrutinizer_keepawake=1\n");
    assert(cs_keep_awake_disable(&paths) == 0);
    assert_file_equals(settings_path, "volume=7\nbrightness=9\n");
    assert_file_absent(keep_awake_state_path);

    assert(unlink(settings_path) == 0);
    assert(cs_keep_awake_enable(&paths) == 0);
    assert_file_equals(settings_path, "screentimeout=0\ncentralscrutinizer_keepawake=1\n");
    assert(cs_keep_awake_disable(&paths) == 0);
    assert_file_absent(settings_path);
    assert_file_absent(keep_awake_state_path);

    write_text_file(settings_path, "volume=7\nscreentimeout=45\n");
    assert(cs_keep_awake_enable(&paths) == 0);
    write_text_file(settings_path, "volume=9\nscreentimeout=120\n");
    assert(cs_keep_awake_disable(&paths) == 0);
    assert_file_equals(settings_path, "volume=9\nscreentimeout=120\n");
    assert_file_absent(keep_awake_state_path);

    remove_tree(sdcard_root);
    return 0;
}
