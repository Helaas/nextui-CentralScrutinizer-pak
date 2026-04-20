#include "cs_settings.h"
#include "cs_util.h"

#include "../third_party/jsmn/jsmn.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int cs_settings_token_eq(const char *json, const jsmntok_t *token, const char *expected) {
    size_t expected_len = strlen(expected);
    size_t token_len;

    if (!json || !token || !expected || token->type != JSMN_STRING) {
        return 0;
    }

    token_len = (size_t) (token->end - token->start);
    return token_len == expected_len && strncmp(json + token->start, expected, token_len) == 0;
}

static int cs_settings_copy_bool(int *out, const char *json, const jsmntok_t *token) {
    size_t token_len;

    if (!out || !json || !token || token->type != JSMN_PRIMITIVE) {
        return -1;
    }

    token_len = (size_t) (token->end - token->start);
    if (token_len == 4 && strncmp(json + token->start, "true", token_len) == 0) {
        *out = 1;
        return 0;
    }
    if (token_len == 5 && strncmp(json + token->start, "false", token_len) == 0) {
        *out = 0;
        return 0;
    }

    return -1;
}

static int cs_settings_ensure_parent_dir(const char *path) {
    char parent[CS_PATH_MAX];
    size_t i;

    if (!path) {
        return -1;
    }

    if (CS_SAFE_SNPRINTF(parent, sizeof(parent), "%s", path) != 0) {
        return -1;
    }

    for (i = strlen(parent); i > 0; --i) {
        if (parent[i - 1] == '/') {
            parent[i - 1] = '\0';
            break;
        }
    }
    if (i == 0 || parent[0] == '\0') {
        return -1;
    }

    for (i = 1; parent[i] != '\0'; ++i) {
        if (parent[i] != '/') {
            continue;
        }
        parent[i] = '\0';
        if (mkdir(parent, 0775) != 0 && errno != EEXIST) {
            return -1;
        }
        parent[i] = '/';
    }

    if (mkdir(parent, 0775) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int cs_settings_fsync_parent_dir(const char *path) {
    char parent[CS_PATH_MAX];
    size_t i;
    int dir_fd;
    int rc = -1;

    if (!path) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(parent, sizeof(parent), "%s", path) != 0) {
        return -1;
    }

    for (i = strlen(parent); i > 0; --i) {
        if (parent[i - 1] == '/') {
            parent[i - 1] = '\0';
            break;
        }
    }
    if (i == 0 || parent[0] == '\0') {
        return -1;
    }

    dir_fd = open(parent, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        return -1;
    }
    if (fsync(dir_fd) == 0) {
        rc = 0;
    }
    if (close(dir_fd) != 0) {
        return -1;
    }

    return rc;
}

int cs_settings_default_terminal_enabled(void) {
#if defined(PLATFORM_MAC)
    return 1;
#else
    return 0;
#endif
}

int cs_settings_default_keep_awake_in_background(void) {
    return 0;
}

int cs_settings_make_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    if (!paths || !buffer || buffer_len == 0) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s/settings.json", paths->shared_state_root);
}

int cs_settings_load(const cs_paths *paths, cs_settings *settings) {
    char path[CS_PATH_MAX];
    FILE *fp = NULL;
    long file_size;
    char *json = NULL;
    jsmn_parser parser;
    jsmntok_t tokens[32];
    int token_count;
    int parsed_terminal_enabled;
    int parsed_keep_awake_in_background;
    int i;

    if (!paths || !settings) {
        return -1;
    }

    settings->terminal_enabled = cs_settings_default_terminal_enabled();
    settings->keep_awake_in_background = cs_settings_default_keep_awake_in_background();
    if (cs_settings_make_path(paths, path, sizeof(path)) != 0) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return errno == ENOENT ? 0 : -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    file_size = ftell(fp);
    if (file_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    json = (char *) malloc((size_t) file_size + 1u);
    if (!json) {
        fclose(fp);
        return -1;
    }
    if (fread(json, 1, (size_t) file_size, fp) != (size_t) file_size) {
        free(json);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    json[file_size] = '\0';

    parsed_terminal_enabled = settings->terminal_enabled;
    parsed_keep_awake_in_background = settings->keep_awake_in_background;
    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, (size_t) file_size, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count >= 1 && tokens[0].type == JSMN_OBJECT) {
        for (i = 1; i + 1 < token_count; i += 2) {
            if (cs_settings_token_eq(json, &tokens[i], "terminal_enabled")) {
                if (cs_settings_copy_bool(&parsed_terminal_enabled, json, &tokens[i + 1]) == 0) {
                    settings->terminal_enabled = parsed_terminal_enabled;
                }
                continue;
            }
            if (cs_settings_token_eq(json, &tokens[i], "keep_awake_in_background")) {
                if (cs_settings_copy_bool(&parsed_keep_awake_in_background, json, &tokens[i + 1]) == 0) {
                    settings->keep_awake_in_background = parsed_keep_awake_in_background;
                }
            }
        }
    }

    free(json);
    return 0;
}

int cs_settings_save(const cs_paths *paths, const cs_settings *settings) {
    char path[CS_PATH_MAX];
    char temp_path[PATH_MAX];
    char body[96];
    int fd;
    FILE *fp = NULL;
    int rc = -1;

    if (!paths || !settings) {
        return -1;
    }
    if (cs_settings_make_path(paths, path, sizeof(path)) != 0) {
        return -1;
    }
    if (cs_settings_ensure_parent_dir(path) != 0) {
        return -1;
    }
    if (strlen(path) + sizeof(".tmpXXXXXX") > sizeof(temp_path)) {
        return -1;
    }

    if (CS_SAFE_SNPRINTF(temp_path, sizeof(temp_path), "%s.tmpXXXXXX", path) != 0) {
        return -1;
    }
    fd = mkstemp(temp_path);
    if (fd < 0) {
        return -1;
    }

    fp = fdopen(fd, "wb");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        return -1;
    }

    if (CS_SAFE_SNPRINTF(body,
                         sizeof(body),
                         "{\"terminal_enabled\":%s,\"keep_awake_in_background\":%s}",
                         settings->terminal_enabled ? "true" : "false",
                         settings->keep_awake_in_background ? "true" : "false")
        != 0) {
        goto cleanup;
    }
    if (fputs(body, fp) == EOF) {
        goto cleanup;
    }
    if (fflush(fp) != 0) {
        goto cleanup;
    }
    if (fsync(fileno(fp)) != 0) {
        goto cleanup;
    }
    if (fclose(fp) != 0) {
        fp = NULL;
        unlink(temp_path);
        return -1;
    }
    fp = NULL;

    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }
    if (cs_settings_fsync_parent_dir(path) != 0) {
        return -1;
    }

    return 0;

cleanup:
    if (fp) {
        fclose(fp);
    }
    unlink(temp_path);
    return rc;
}
