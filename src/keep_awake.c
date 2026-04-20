#include "cs_keep_awake.h"

#include "cs_build_info.h"
#include "cs_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CS_KEEP_AWAKE_MARKER_KEY "centralscrutinizer_keepawake"
#define CS_KEEP_AWAKE_MARKER_LINE "centralscrutinizer_keepawake=1"
#define CS_KEEP_AWAKE_TIMEOUT_KEY "screentimeout"
#define CS_KEEP_AWAKE_TIMEOUT_OVERRIDE_LINE "screentimeout=0"

typedef struct cs_keep_awake_state {
    int file_existed;
    int had_screentimeout;
    char original_screentimeout[CS_PATH_MAX];
} cs_keep_awake_state;

typedef struct cs_keep_awake_buffer {
    char *data;
    size_t len;
    size_t cap;
} cs_keep_awake_buffer;

static int cs_keep_awake_is_supported_platform(const char *platform_name) {
    return platform_name && (strcmp(platform_name, "tg5040") == 0 || strcmp(platform_name, "tg5050") == 0
                             || strcmp(platform_name, "my355") == 0);
}

const char *cs_keep_awake_platform_name(void) {
    const char *override = getenv("CS_PLATFORM_NAME_OVERRIDE");

    if (override && override[0] != '\0') {
        return override;
    }

    return cs_build_info_platform_name();
}

int cs_keep_awake_current_platform_uses_settings_override(void) {
    return cs_keep_awake_is_supported_platform(cs_keep_awake_platform_name());
}

static int cs_keep_awake_make_settings_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    if (!paths || !buffer || buffer_len == 0) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s/minuisettings.txt", paths->shared_userdata_root);
}

static int cs_keep_awake_make_state_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    if (!paths || !buffer || buffer_len == 0) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s/nextui-keep-awake-state.txt", paths->shared_state_root);
}

static int cs_keep_awake_line_has_key(const char *line, size_t line_len, const char *key) {
    size_t key_len = strlen(key);

    return line && key && line_len > key_len && strncmp(line, key, key_len) == 0 && line[key_len] == '=';
}

static int cs_keep_awake_buffer_reserve(cs_keep_awake_buffer *buffer, size_t additional) {
    char *next_data;
    size_t required;
    size_t next_cap;

    if (!buffer) {
        return -1;
    }

    required = buffer->len + additional + 1u;
    if (required <= buffer->cap) {
        return 0;
    }

    next_cap = buffer->cap == 0 ? 128u : buffer->cap;
    while (next_cap < required) {
        next_cap *= 2u;
    }

    next_data = (char *) realloc(buffer->data, next_cap);
    if (!next_data) {
        return -1;
    }

    buffer->data = next_data;
    buffer->cap = next_cap;
    return 0;
}

static int cs_keep_awake_buffer_append_bytes(cs_keep_awake_buffer *buffer, const char *data, size_t len) {
    if (!buffer || (!data && len != 0)) {
        return -1;
    }
    if (cs_keep_awake_buffer_reserve(buffer, len) != 0) {
        return -1;
    }
    if (len > 0) {
        memcpy(buffer->data + buffer->len, data, len);
        buffer->len += len;
    }
    buffer->data[buffer->len] = '\0';
    return 0;
}

static int cs_keep_awake_buffer_append_line(cs_keep_awake_buffer *buffer, const char *line) {
    size_t len;

    if (!buffer || !line) {
        return -1;
    }

    len = strlen(line);
    if (cs_keep_awake_buffer_append_bytes(buffer, line, len) != 0) {
        return -1;
    }
    return cs_keep_awake_buffer_append_bytes(buffer, "\n", 1);
}

static int cs_keep_awake_buffer_ensure_trailing_newline(cs_keep_awake_buffer *buffer) {
    if (!buffer) {
        return -1;
    }
    if (buffer->len == 0 || buffer->data[buffer->len - 1] == '\n') {
        return 0;
    }
    return cs_keep_awake_buffer_append_bytes(buffer, "\n", 1);
}

static void cs_keep_awake_buffer_free(cs_keep_awake_buffer *buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

static int cs_keep_awake_next_line(const char *contents,
                                   size_t contents_len,
                                   size_t *offset,
                                   const char **line_out,
                                   size_t *line_len_out,
                                   int *had_newline_out) {
    size_t start;
    size_t end;
    int had_newline = 0;

    if (!offset || !line_out || !line_len_out || !had_newline_out) {
        return 0;
    }
    if (*offset >= contents_len) {
        return 0;
    }

    start = *offset;
    end = start;
    while (end < contents_len && contents[end] != '\n') {
        ++end;
    }
    if (end < contents_len && contents[end] == '\n') {
        had_newline = 1;
    }
    *offset = had_newline ? end + 1u : end;
    if (end > start && contents[end - 1] == '\r') {
        --end;
    }

    *line_out = contents + start;
    *line_len_out = end - start;
    *had_newline_out = had_newline;
    return 1;
}

static int cs_keep_awake_ensure_parent_dir(const char *path) {
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

static int cs_keep_awake_fsync_parent_dir(const char *path) {
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

static int cs_keep_awake_write_atomic(const char *path, const char *contents, size_t contents_len) {
    char temp_path[CS_PATH_MAX + 16];
    int fd;
    ssize_t written_total = 0;

    if (!path || (!contents && contents_len != 0)) {
        return -1;
    }
    if (cs_keep_awake_ensure_parent_dir(path) != 0) {
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

    while (written_total < (ssize_t) contents_len) {
        ssize_t written = write(fd, contents + written_total, contents_len - (size_t) written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            unlink(temp_path);
            return -1;
        }
        written_total += written;
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp_path);
        return -1;
    }
    if (close(fd) != 0) {
        unlink(temp_path);
        return -1;
    }
    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }
    if (cs_keep_awake_fsync_parent_dir(path) != 0) {
        return -1;
    }

    return 0;
}

static int cs_keep_awake_remove_file(const char *path) {
    if (!path) {
        return -1;
    }
    if (unlink(path) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    return cs_keep_awake_fsync_parent_dir(path);
}

static int cs_keep_awake_read_file(const char *path, char **contents_out, size_t *contents_len_out) {
    FILE *fp = NULL;
    char *contents = NULL;
    long file_size;

    if (!path || !contents_out || !contents_len_out) {
        return -1;
    }

    *contents_out = NULL;
    *contents_len_out = 0;
    fp = fopen(path, "rb");
    if (!fp) {
        return errno == ENOENT ? 1 : -1;
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

    contents = (char *) malloc((size_t) file_size + 1u);
    if (!contents) {
        fclose(fp);
        return -1;
    }
    if (file_size > 0 && fread(contents, 1, (size_t) file_size, fp) != (size_t) file_size) {
        free(contents);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    contents[file_size] = '\0';
    *contents_out = contents;
    *contents_len_out = (size_t) file_size;
    return 0;
}

static int cs_keep_awake_save_state(const char *state_path, const cs_keep_awake_state *state) {
    char body[(CS_PATH_MAX * 2) + 64];

    if (!state_path || !state) {
        return -1;
    }
    if (strchr(state->original_screentimeout, '\n') || strchr(state->original_screentimeout, '\r')) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(body,
                         sizeof(body),
                         "file_existed=%d\nhad_screentimeout=%d\noriginal_screentimeout=%s\n",
                         state->file_existed ? 1 : 0,
                         state->had_screentimeout ? 1 : 0,
                         state->original_screentimeout)
        != 0) {
        return -1;
    }

    return cs_keep_awake_write_atomic(state_path, body, strlen(body));
}

static int cs_keep_awake_load_state(const char *state_path, cs_keep_awake_state *state, int *exists_out) {
    char *contents = NULL;
    size_t contents_len = 0;
    size_t offset = 0;
    const char *line;
    size_t line_len;
    int had_newline;
    int read_rc;
    int saw_file_existed = 0;
    int saw_had_screentimeout = 0;
    int saw_original = 0;

    if (!state_path || !state || !exists_out) {
        return -1;
    }

    memset(state, 0, sizeof(*state));
    *exists_out = 0;
    read_rc = cs_keep_awake_read_file(state_path, &contents, &contents_len);
    if (read_rc == 1) {
        return 0;
    }
    if (read_rc != 0) {
        return -1;
    }

    *exists_out = 1;
    while (cs_keep_awake_next_line(contents, contents_len, &offset, &line, &line_len, &had_newline)) {
        (void) had_newline;

        if (line_len >= strlen("file_existed=") && strncmp(line, "file_existed=", strlen("file_existed=")) == 0) {
            const char *value = line + strlen("file_existed=");

            if (line_len != strlen("file_existed=0") && line_len != strlen("file_existed=1")) {
                free(contents);
                return -1;
            }
            state->file_existed = *value == '1' ? 1 : 0;
            saw_file_existed = 1;
            continue;
        }
        if (line_len >= strlen("had_screentimeout=")
            && strncmp(line, "had_screentimeout=", strlen("had_screentimeout=")) == 0) {
            const char *value = line + strlen("had_screentimeout=");

            if (line_len != strlen("had_screentimeout=0") && line_len != strlen("had_screentimeout=1")) {
                free(contents);
                return -1;
            }
            state->had_screentimeout = *value == '1' ? 1 : 0;
            saw_had_screentimeout = 1;
            continue;
        }
        if (line_len >= strlen("original_screentimeout=")
            && strncmp(line, "original_screentimeout=", strlen("original_screentimeout=")) == 0) {
            size_t value_len = line_len - strlen("original_screentimeout=");

            if (value_len >= sizeof(state->original_screentimeout)) {
                free(contents);
                return -1;
            }
            memcpy(state->original_screentimeout, line + strlen("original_screentimeout="), value_len);
            state->original_screentimeout[value_len] = '\0';
            saw_original = 1;
            continue;
        }
        if (line_len != 0) {
            free(contents);
            return -1;
        }
    }

    free(contents);
    if (!saw_file_existed || !saw_had_screentimeout || !saw_original) {
        return -1;
    }
    if (!state->had_screentimeout) {
        state->original_screentimeout[0] = '\0';
    }

    return 0;
}

static int cs_keep_awake_rewrite_enable(const char *input,
                                        size_t input_len,
                                        cs_keep_awake_state *state,
                                        char **output_out,
                                        size_t *output_len_out) {
    cs_keep_awake_buffer output = {0};
    size_t offset = 0;
    const char *line;
    size_t line_len;
    int had_newline;
    int wrote_timeout = 0;

    if (!state || !output_out || !output_len_out) {
        return -1;
    }

    while (input && cs_keep_awake_next_line(input, input_len, &offset, &line, &line_len, &had_newline)) {
        if (cs_keep_awake_line_has_key(line, line_len, CS_KEEP_AWAKE_MARKER_KEY)) {
            continue;
        }
        if (cs_keep_awake_line_has_key(line, line_len, CS_KEEP_AWAKE_TIMEOUT_KEY)) {
            if (!state->had_screentimeout) {
                if (line_len >= sizeof(state->original_screentimeout)) {
                    cs_keep_awake_buffer_free(&output);
                    return -1;
                }
                memcpy(state->original_screentimeout, line, line_len);
                state->original_screentimeout[line_len] = '\0';
                state->had_screentimeout = 1;
            }
            if (!wrote_timeout) {
                if (cs_keep_awake_buffer_append_line(&output, CS_KEEP_AWAKE_TIMEOUT_OVERRIDE_LINE) != 0) {
                    cs_keep_awake_buffer_free(&output);
                    return -1;
                }
                wrote_timeout = 1;
            }
            continue;
        }

        if (cs_keep_awake_buffer_append_bytes(&output, line, line_len) != 0
            || (had_newline && cs_keep_awake_buffer_append_bytes(&output, "\n", 1) != 0)) {
            cs_keep_awake_buffer_free(&output);
            return -1;
        }
    }

    if (!wrote_timeout) {
        if (cs_keep_awake_buffer_ensure_trailing_newline(&output) != 0
            || cs_keep_awake_buffer_append_line(&output, CS_KEEP_AWAKE_TIMEOUT_OVERRIDE_LINE) != 0) {
            cs_keep_awake_buffer_free(&output);
            return -1;
        }
    }
    if (cs_keep_awake_buffer_ensure_trailing_newline(&output) != 0
        || cs_keep_awake_buffer_append_line(&output, CS_KEEP_AWAKE_MARKER_LINE) != 0) {
        cs_keep_awake_buffer_free(&output);
        return -1;
    }

    *output_out = output.data;
    *output_len_out = output.len;
    return 0;
}

static int cs_keep_awake_restore_from_override(const char *input,
                                               size_t input_len,
                                               const cs_keep_awake_state *state,
                                               char **output_out,
                                               size_t *output_len_out,
                                               int *marker_present_out) {
    cs_keep_awake_buffer output = {0};
    size_t offset = 0;
    const char *line;
    size_t line_len;
    int had_newline;
    int marker_present = 0;
    int restored_timeout = 0;

    if (!state || !output_out || !output_len_out || !marker_present_out) {
        return -1;
    }

    while (input && cs_keep_awake_next_line(input, input_len, &offset, &line, &line_len, &had_newline)) {
        if (cs_keep_awake_line_has_key(line, line_len, CS_KEEP_AWAKE_MARKER_KEY)) {
            marker_present = 1;
            continue;
        }
        if (cs_keep_awake_line_has_key(line, line_len, CS_KEEP_AWAKE_TIMEOUT_KEY)) {
            if (state->had_screentimeout && !restored_timeout) {
                if (cs_keep_awake_buffer_append_line(&output, state->original_screentimeout) != 0) {
                    cs_keep_awake_buffer_free(&output);
                    return -1;
                }
                restored_timeout = 1;
            }
            continue;
        }

        if (cs_keep_awake_buffer_append_bytes(&output, line, line_len) != 0
            || (had_newline && cs_keep_awake_buffer_append_bytes(&output, "\n", 1) != 0)) {
            cs_keep_awake_buffer_free(&output);
            return -1;
        }
    }

    if (state->had_screentimeout && !restored_timeout) {
        if (cs_keep_awake_buffer_ensure_trailing_newline(&output) != 0
            || cs_keep_awake_buffer_append_line(&output, state->original_screentimeout) != 0) {
            cs_keep_awake_buffer_free(&output);
            return -1;
        }
    }

    *marker_present_out = marker_present;
    *output_out = output.data;
    *output_len_out = output.len;
    return 0;
}

int cs_keep_awake_enable(const cs_paths *paths) {
    cs_keep_awake_state state = {0};
    char settings_path[CS_PATH_MAX];
    char state_path[CS_PATH_MAX];
    char *settings_contents = NULL;
    char *updated_contents = NULL;
    size_t settings_len = 0;
    size_t updated_len = 0;
    int state_exists = 0;
    int read_rc;

    if (!paths) {
        return -1;
    }
    if (!cs_keep_awake_current_platform_uses_settings_override()) {
        return 0;
    }
    if (cs_keep_awake_make_settings_path(paths, settings_path, sizeof(settings_path)) != 0
        || cs_keep_awake_make_state_path(paths, state_path, sizeof(state_path)) != 0) {
        return -1;
    }
    if (cs_keep_awake_load_state(state_path, &state, &state_exists) != 0) {
        return -1;
    }
    if (state_exists) {
        return 0;
    }

    read_rc = cs_keep_awake_read_file(settings_path, &settings_contents, &settings_len);
    if (read_rc == 1) {
        state.file_existed = 0;
    } else if (read_rc == 0) {
        state.file_existed = 1;
    } else {
        return -1;
    }

    if (cs_keep_awake_rewrite_enable(settings_contents, settings_len, &state, &updated_contents, &updated_len) != 0) {
        free(settings_contents);
        return -1;
    }
    free(settings_contents);

    if (cs_keep_awake_save_state(state_path, &state) != 0) {
        free(updated_contents);
        return -1;
    }
    if (cs_keep_awake_write_atomic(settings_path, updated_contents, updated_len) != 0) {
        free(updated_contents);
        (void) cs_keep_awake_remove_file(state_path);
        return -1;
    }

    free(updated_contents);
    return 0;
}

int cs_keep_awake_disable(const cs_paths *paths) {
    cs_keep_awake_state state = {0};
    char settings_path[CS_PATH_MAX];
    char state_path[CS_PATH_MAX];
    char *settings_contents = NULL;
    char *restored_contents = NULL;
    size_t settings_len = 0;
    size_t restored_len = 0;
    int marker_present = 0;
    int state_exists = 0;
    int read_rc;

    if (!paths) {
        return -1;
    }
    if (!cs_keep_awake_current_platform_uses_settings_override()) {
        return 0;
    }
    if (cs_keep_awake_make_settings_path(paths, settings_path, sizeof(settings_path)) != 0
        || cs_keep_awake_make_state_path(paths, state_path, sizeof(state_path)) != 0) {
        return -1;
    }
    if (cs_keep_awake_load_state(state_path, &state, &state_exists) != 0) {
        return -1;
    }
    if (!state_exists) {
        return 0;
    }

    read_rc = cs_keep_awake_read_file(settings_path, &settings_contents, &settings_len);
    if (read_rc == 1) {
        return cs_keep_awake_remove_file(state_path);
    }
    if (read_rc != 0) {
        return -1;
    }

    if (cs_keep_awake_restore_from_override(settings_contents,
                                            settings_len,
                                            &state,
                                            &restored_contents,
                                            &restored_len,
                                            &marker_present)
        != 0) {
        free(settings_contents);
        return -1;
    }
    free(settings_contents);

    if (!marker_present) {
        free(restored_contents);
        return cs_keep_awake_remove_file(state_path);
    }

    if (!state.file_existed && restored_len == 0) {
        free(restored_contents);
        if (cs_keep_awake_remove_file(settings_path) != 0) {
            return -1;
        }
    } else {
        if (cs_keep_awake_write_atomic(settings_path, restored_contents, restored_len) != 0) {
            free(restored_contents);
            return -1;
        }
        free(restored_contents);
    }

    return cs_keep_awake_remove_file(state_path);
}
