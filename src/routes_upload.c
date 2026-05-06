#include "cs_app.h"
#include "cs_library.h"
#include "cs_platforms.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"
#include "cs_uploads.h"

#include "civetweb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CS_UPLOAD_MAX_FILES 32
#define CS_UPLOAD_MAX_DIRECTORIES 32
#define CS_UPLOAD_MAX_REQUEST_BYTES_DEFAULT (8LL * 1024 * 1024 * 1024)
#define CS_UPLOAD_MAX_BIOS_FILE_BYTES_DEFAULT (64LL * 1024 * 1024)
#define CS_UPLOAD_PREVIEW_TRACK_MAX 256
#define CS_UPLOAD_PREVIEW_RESULT_MAX CS_UPLOAD_PREVIEW_TRACK_MAX

typedef struct cs_upload_request {
    cs_app *app;
    char scope[32];
    char tag[64];
    char path[CS_PATH_MAX];
    char file_names[CS_UPLOAD_MAX_FILES][256];
    char relative_dirs[CS_UPLOAD_MAX_FILES][CS_PATH_MAX];
    char preview_file_names[CS_UPLOAD_MAX_FILES][256];
    char preview_relative_dirs[CS_UPLOAD_MAX_FILES][CS_PATH_MAX];
    char directories[CS_UPLOAD_MAX_DIRECTORIES][CS_PATH_MAX];
    char final_root[CS_PATH_MAX];
    unsigned int path_flags;
    int metadata_ready;
    int failed;
    int overwrite_existing;
    cs_upload_plan plans[CS_UPLOAD_MAX_FILES];
    long long file_sizes[CS_UPLOAD_MAX_FILES];
    size_t directory_count;
    size_t plan_count;
    size_t preview_file_count;
    size_t stored_count;
} cs_upload_request;

typedef enum cs_upload_preview_kind {
    CS_UPLOAD_PREVIEW_OVERWRITE = 0,
    CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY = 1,
    CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE = 2,
} cs_upload_preview_kind;

typedef struct cs_upload_preview_conflict {
    char path[CS_PATH_MAX];
    cs_upload_preview_kind kind;
} cs_upload_preview_conflict;

typedef struct cs_upload_preview_result {
    cs_upload_preview_conflict overwriteable[CS_UPLOAD_PREVIEW_RESULT_MAX];
    cs_upload_preview_conflict blocking[CS_UPLOAD_PREVIEW_RESULT_MAX];
    char seen_paths[CS_UPLOAD_PREVIEW_TRACK_MAX][CS_PATH_MAX];
    cs_upload_preview_kind seen_kinds[CS_UPLOAD_PREVIEW_TRACK_MAX];
    size_t overwriteable_count;
    size_t blocking_count;
    size_t overwriteable_preview_count;
    size_t blocking_preview_count;
    size_t seen_count;
} cs_upload_preview_result;

static int cs_method_is(const struct mg_connection *conn, const char *method) {
    const struct mg_request_info *request = mg_get_request_info(conn);

    return request && request->request_method && strcmp(request->request_method, method) == 0;
}

static int cs_write_json(struct mg_connection *conn, int status, const char *reason, const char *body) {
    size_t body_len = body ? strlen(body) : 0;

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status,
              reason,
              body_len,
              body ? body : "");
    return 1;
}

static int cs_write_upload_conflict_response(struct mg_connection *conn, const char *error, const char *path) {
    const char *error_name = error && strcmp(error, "upload_type_conflict") == 0 ? "upload_type_conflict" : "upload_conflict";

    if (!path || path[0] == '\0') {
        return cs_write_json(conn,
                             409,
                             "Conflict",
                             strcmp(error_name, "upload_type_conflict") == 0
                                 ? "{\"ok\":false,\"error\":\"upload_type_conflict\"}"
                                 : "{\"ok\":false,\"error\":\"upload_conflict\"}");
    }

    if (mg_printf(conn,
                  "HTTP/1.1 409 Conflict\r\n"
                  "Content-Type: application/json\r\n"
                  CS_SERVER_SECURITY_HEADERS_HTTP
                  "Cache-Control: no-store\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n")
            <= 0) {
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"ok\":false,\"error\":\"") != 0
        || cs_routes_stream_literal(conn, error_name) != 0
        || cs_routes_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_routes_stream_escaped_string(conn, path) != 0
        || cs_routes_stream_literal(conn, "\"}") != 0
        || mg_send_chunk(conn, "", 0) < 0) {
        (void) mg_send_chunk(conn, "", 0);
    }

    return 1;
}

static int cs_write_upload_errno_response(struct mg_connection *conn, const char *path) {
    if (errno == EEXIST) {
        return cs_write_upload_conflict_response(conn, "upload_conflict", path);
    }
    if (errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
        return cs_write_upload_conflict_response(conn, "upload_type_conflict", path);
    }

    return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
}

static int cs_upload_filename_is_safe(const char *filename) {
    return cs_validate_path_component_with_flags(filename, CS_PATH_FLAG_ALLOW_HIDDEN) == 0;
}

static long long cs_upload_env_limit_bytes(const char *name, long long fallback) {
    const char *value = getenv(name);
    char *end = NULL;
    long long parsed;

    if (!value || value[0] == '\0') {
        return fallback;
    }

    parsed = strtoll(value, &end, 10);
    if (!end || *end != '\0' || parsed <= 0) {
        return fallback;
    }

    return parsed;
}

static long long cs_upload_request_limit_bytes(void) {
    return cs_upload_env_limit_bytes("CS_MAX_UPLOAD_REQUEST_BYTES", CS_UPLOAD_MAX_REQUEST_BYTES_DEFAULT);
}

static long long cs_upload_file_limit_bytes(const cs_upload_request *state) {
    cs_browser_scope scope;

    if (!state) {
        return -1;
    }

    scope = cs_browser_scope_parse(state->scope);
    if (scope == CS_SCOPE_BIOS) {
        return cs_upload_env_limit_bytes("CS_MAX_BIOS_UPLOAD_BYTES", CS_UPLOAD_MAX_BIOS_FILE_BYTES_DEFAULT);
    }

    return cs_upload_request_limit_bytes();
}

static int cs_upload_split_client_path(const char *client_path,
                                       char *relative_dir,
                                       size_t relative_dir_size,
                                       char *filename,
                                       size_t filename_size) {
    const char *leaf;
    size_t dir_length;

    if (!client_path || !relative_dir || relative_dir_size == 0 || !filename || filename_size == 0) {
        return -1;
    }

    leaf = strrchr(client_path, '/');
    if (!leaf) {
        if (!cs_upload_filename_is_safe(client_path)) {
            return -1;
        }
        if (snprintf(relative_dir, relative_dir_size, "%s", "") >= (int) relative_dir_size
            || snprintf(filename, filename_size, "%s", client_path) >= (int) filename_size) {
            return -1;
        }
        return 0;
    }

    dir_length = (size_t) (leaf - client_path);
    if (dir_length == 0 || client_path[dir_length + 1] == '\0') {
        return -1;
    }
    if (dir_length >= relative_dir_size) {
        return -1;
    }

    memcpy(relative_dir, client_path, dir_length);
    relative_dir[dir_length] = '\0';
    if (cs_validate_relative_path_with_flags(relative_dir, CS_PATH_FLAG_ALLOW_EMPTY | CS_PATH_FLAG_ALLOW_HIDDEN) != 0) {
        return -1;
    }
    if (!cs_upload_filename_is_safe(leaf + 1)) {
        return -1;
    }
    if (snprintf(filename, filename_size, "%s", leaf + 1) >= (int) filename_size) {
        return -1;
    }

    return 0;
}

static int cs_upload_join_relative_path(const char *base, const char *child, char *buffer, size_t buffer_size) {
    int written;

    if (!base || !child || !buffer || buffer_size == 0) {
        return -1;
    }

    if (base[0] != '\0' && child[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s/%s", base, child);
    } else if (base[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s", base);
    } else {
        written = snprintf(buffer, buffer_size, "%s", child);
    }

    return written < 0 || (size_t) written >= buffer_size ? -1 : 0;
}

static void cs_upload_cleanup_temp_files(cs_upload_request *state) {
    size_t i;

    if (!state) {
        return;
    }

    for (i = 0; i < state->plan_count; ++i) {
        if (state->plans[i].temp_path[0] != '\0') {
            (void) remove(state->plans[i].temp_path);
        }
    }
}

static int cs_prepare_upload_metadata(cs_upload_request *state) {
    cs_browser_scope scope;
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;

    if (!state || !state->app) {
        return -1;
    }
    if (state->metadata_ready) {
        return 0;
    }

    scope = cs_browser_scope_parse(state->scope);
    if (scope == CS_SCOPE_INVALID) {
        return -1;
    }
    if (cs_browser_scope_requires_platform(scope)) {
        if (cs_platform_resolve(&state->app->paths, state->tag, &resolved_platform) != 0) {
            return -1;
        }
        platform = &resolved_platform;
    }
    state->path_flags =
        cs_browser_scope_allows_hidden_for_platform(scope, platform) ? CS_PATH_FLAG_ALLOW_HIDDEN : 0;
    if (cs_browser_root_for_scope(&state->app->paths,
                                  scope,
                                  platform,
                                  state->final_root,
                                  sizeof(state->final_root))
        != 0) {
        return -1;
    }

    state->metadata_ready = 1;
    return 0;
}

static int cs_upload_boolean_field(const char *value, size_t valuelen) {
    return (valuelen == 1 && value[0] == '1') || (valuelen == 4 && strncmp(value, "true", 4) == 0);
}

static int cs_upload_field_found(const char *key,
                                 const char *filename,
                                 char *path,
                                 size_t pathlen,
                                 void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    cs_upload_plan *plan;
    int written;

    if (!state || !state->app || !path || pathlen == 0) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (state->failed) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (!key) {
        return MG_FORM_FIELD_STORAGE_SKIP;
    }
    if (strcmp(key, "file") != 0) {
        return MG_FORM_FIELD_STORAGE_GET;
    }
    if (state->plan_count >= CS_UPLOAD_MAX_FILES || !filename || filename[0] == '\0') {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    plan = &state->plans[state->plan_count];
    if (cs_upload_split_client_path(filename,
                                    state->relative_dirs[state->plan_count],
                                    sizeof(state->relative_dirs[state->plan_count]),
                                    state->file_names[state->plan_count],
                                    sizeof(state->file_names[state->plan_count]))
            != 0
        || cs_upload_reserve_temp_path(&state->app->paths,
                                       state->file_names[state->plan_count],
                                       plan->temp_path,
                                       sizeof(plan->temp_path))
               != 0) {
        if (plan->temp_path[0] != '\0') {
            (void) remove(plan->temp_path);
            plan->temp_path[0] = '\0';
        }
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    written = snprintf(path, pathlen, "%s", plan->temp_path);
    if (written < 0 || (size_t) written >= pathlen) {
        (void) remove(plan->temp_path);
        plan->temp_path[0] = '\0';
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    state->plan_count += 1;
    return MG_FORM_FIELD_STORAGE_STORE;
}

static int cs_upload_field_get(const char *key, const char *value, size_t valuelen, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    size_t copy_len;
    char *target = NULL;
    size_t target_size = 0;

    if (!state || !key || !value) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    if (strcmp(key, "scope") == 0) {
        target = state->scope;
        target_size = sizeof(state->scope);
    } else if (strcmp(key, "tag") == 0) {
        target = state->tag;
        target_size = sizeof(state->tag);
    } else if (strcmp(key, "path") == 0) {
        target = state->path;
        target_size = sizeof(state->path);
    } else if (strcmp(key, "overwrite") == 0) {
        state->overwrite_existing = cs_upload_boolean_field(value, valuelen);
        return MG_FORM_FIELD_HANDLE_NEXT;
    } else if (strcmp(key, "directory") == 0) {
        if (state->directory_count >= CS_UPLOAD_MAX_DIRECTORIES || valuelen == 0
            || valuelen >= sizeof(state->directories[state->directory_count])) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }
        memcpy(state->directories[state->directory_count], value, valuelen);
        state->directories[state->directory_count][valuelen] = '\0';
        state->directory_count += 1;
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    if (!target || target_size == 0) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    copy_len = valuelen < target_size - 1 ? valuelen : target_size - 1;
    memcpy(target, value, copy_len);
    target[copy_len] = '\0';
    return MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_upload_preview_field_found(const char *key,
                                         const char *filename,
                                         char *path,
                                         size_t pathlen,
                                         void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;

    (void) filename;
    (void) path;
    (void) pathlen;

    if (!state || state->failed) {
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (!key) {
        return MG_FORM_FIELD_STORAGE_SKIP;
    }
    if (strcmp(key, "file") == 0) {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    return MG_FORM_FIELD_STORAGE_GET;
}

static int cs_upload_preview_field_get(const char *key, const char *value, size_t valuelen, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    int common_status;

    common_status = cs_upload_field_get(key, value, valuelen, user_data);
    if (common_status != MG_FORM_FIELD_HANDLE_NEXT || !key || !value) {
        return common_status;
    }

    if (strcmp(key, "file_path") == 0) {
        char client_path[CS_PATH_MAX];

        if (state->preview_file_count >= CS_UPLOAD_MAX_FILES || valuelen == 0 || valuelen >= sizeof(client_path)) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }

        memcpy(client_path, value, valuelen);
        client_path[valuelen] = '\0';
        if (cs_upload_split_client_path(client_path,
                                        state->preview_relative_dirs[state->preview_file_count],
                                        sizeof(state->preview_relative_dirs[state->preview_file_count]),
                                        state->preview_file_names[state->preview_file_count],
                                        sizeof(state->preview_file_names[state->preview_file_count]))
            != 0) {
            state->failed = 1;
            return MG_FORM_FIELD_HANDLE_ABORT;
        }

        state->preview_file_count += 1;
    }

    return state->failed ? MG_FORM_FIELD_HANDLE_ABORT : MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_upload_field_store(const char *path, long long file_size, void *user_data) {
    cs_upload_request *state = (cs_upload_request *) user_data;
    size_t i;

    if (!state || !path || file_size < 0) {
        return MG_FORM_FIELD_HANDLE_ABORT;
    }

    for (i = 0; i < state->plan_count; ++i) {
        if (strcmp(path, state->plans[i].temp_path) == 0) {
            state->file_sizes[i] = file_size;
            state->stored_count += 1;
            return MG_FORM_FIELD_HANDLE_NEXT;
        }
    }

    state->failed = 1;
    return MG_FORM_FIELD_HANDLE_ABORT;
}

static int cs_upload_preview_field_store(const char *path, long long file_size, void *user_data) {
    (void) path;
    (void) file_size;
    if (user_data) {
        ((cs_upload_request *) user_data)->failed = 1;
    }
    return MG_FORM_FIELD_HANDLE_ABORT;
}

static int cs_upload_preview_conflict_seen(const cs_upload_preview_result *result,
                                           const char *path,
                                           cs_upload_preview_kind kind) {
    size_t i;

    if (!result || !path) {
        return 0;
    }

    for (i = 0; i < result->seen_count; ++i) {
        if (result->seen_kinds[i] == kind && strcmp(result->seen_paths[i], path) == 0) {
            return 1;
        }
    }

    return 0;
}

static void cs_upload_preview_record(cs_upload_preview_result *result,
                                     const char *path,
                                     cs_upload_preview_kind kind,
                                     int blocking) {
    cs_upload_preview_conflict *preview_list;
    size_t *preview_count;
    size_t *total_count;

    if (!result || !path || path[0] == '\0' || cs_upload_preview_conflict_seen(result, path, kind)) {
        return;
    }

    if (result->seen_count < CS_UPLOAD_PREVIEW_TRACK_MAX) {
        (void) snprintf(result->seen_paths[result->seen_count],
                        sizeof(result->seen_paths[result->seen_count]),
                        "%s",
                        path);
        result->seen_kinds[result->seen_count] = kind;
        result->seen_count += 1;
    }

    preview_list = blocking ? result->blocking : result->overwriteable;
    preview_count = blocking ? &result->blocking_preview_count : &result->overwriteable_preview_count;
    total_count = blocking ? &result->blocking_count : &result->overwriteable_count;

    *total_count += 1;
    if (*preview_count >= CS_UPLOAD_PREVIEW_RESULT_MAX) {
        return;
    }

    (void) snprintf(preview_list[*preview_count].path, sizeof(preview_list[*preview_count].path), "%s", path);
    preview_list[*preview_count].kind = kind;
    *preview_count += 1;
}

static int cs_upload_preview_scan_required_directories(const cs_upload_request *state,
                                                       const char *relative_path,
                                                       cs_upload_preview_result *result) {
    const char *cursor;
    char current[CS_PATH_MAX] = {0};

    if (!state || !relative_path || !result || relative_path[0] == '\0') {
        return 0;
    }

    cursor = relative_path;
    while (*cursor != '\0') {
        const char *slash = strchr(cursor, '/');
        size_t component_len = slash ? (size_t) (slash - cursor) : strlen(cursor);
        char component[CS_PATH_MAX];
        char absolute_path[CS_PATH_MAX];
        char next_path[CS_PATH_MAX];
        struct stat st;

        if (component_len == 0 || component_len >= sizeof(component)) {
            return -1;
        }

        memcpy(component, cursor, component_len);
        component[component_len] = '\0';
        /* next_path is built component-by-component inside this loop, so it is never empty and
         * does not need the ALLOW_EMPTY flag that the file-specific resolver uses.
         */
        if (cs_upload_join_relative_path(current, component, next_path, sizeof(next_path)) != 0
            || cs_resolve_path_under_root_with_flags(state->final_root,
                                                     next_path,
                                                     state->path_flags,
                                                     absolute_path,
                                                     sizeof(absolute_path))
                   != 0) {
            return -1;
        }

        if (lstat(absolute_path, &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                cs_upload_preview_record(result, next_path, CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE, 1);
                return 1;
            }
        } else if (errno != ENOENT) {
            return -1;
        }

        if (snprintf(current, sizeof(current), "%s", next_path) >= (int) sizeof(current)) {
            return -1;
        }
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }

    return 0;
}

static int cs_upload_preview_scan_directory(const cs_upload_request *state,
                                            const char *directory,
                                            cs_upload_preview_result *result) {
    char upload_dir[CS_PATH_MAX];
    int status;

    if (!state || !directory || !result) {
        return -1;
    }
    if (cs_upload_join_relative_path(state->path, directory, upload_dir, sizeof(upload_dir)) != 0 || upload_dir[0] == '\0') {
        return -1;
    }

    status = cs_upload_preview_scan_required_directories(state, upload_dir, result);
    return status < 0 ? -1 : 0;
}

static int cs_upload_preview_scan_file(const cs_upload_request *state,
                                       const char *relative_dir,
                                       const char *filename,
                                       cs_upload_preview_result *result) {
    char upload_dir[CS_PATH_MAX];
    char resolved_dir[CS_PATH_MAX];
    char upload_path[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    struct stat st;
    int directory_status;

    if (!state || !filename || !result) {
        return -1;
    }
    if (cs_upload_join_relative_path(state->path, relative_dir ? relative_dir : "", upload_dir, sizeof(upload_dir)) != 0) {
        return -1;
    }

    directory_status = cs_upload_preview_scan_required_directories(state, upload_dir, result);
    if (directory_status != 0) {
        return directory_status < 0 ? -1 : 0;
    }
    if (cs_resolve_path_under_root_with_flags(state->final_root,
                                              upload_dir,
                                              state->path_flags | CS_PATH_FLAG_ALLOW_EMPTY,
                                              resolved_dir,
                                              sizeof(resolved_dir))
        != 0
        || cs_upload_join_relative_path(upload_dir, filename, upload_path, sizeof(upload_path)) != 0
        || snprintf(final_path, sizeof(final_path), "%s/%s", resolved_dir, filename) >= (int) sizeof(final_path)) {
        return -1;
    }

    if (lstat(final_path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (S_ISREG(st.st_mode)) {
        cs_upload_preview_record(result, upload_path, CS_UPLOAD_PREVIEW_OVERWRITE, 0);
        return 0;
    }

    cs_upload_preview_record(result, upload_path, CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY, 1);
    return 0;
}

static const char *cs_upload_preview_kind_name(cs_upload_preview_kind kind) {
    switch (kind) {
        case CS_UPLOAD_PREVIEW_OVERWRITE:
            return "overwrite";
        case CS_UPLOAD_PREVIEW_FILE_OVER_DIRECTORY:
            return "file-over-directory";
        case CS_UPLOAD_PREVIEW_DIRECTORY_OVER_FILE:
            return "directory-over-file";
        default:
            return "overwrite";
    }
}

static int cs_upload_preview_write_list(struct mg_connection *conn,
                                        const cs_upload_preview_conflict *items,
                                        size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "{\"path\":\"") != 0
            || cs_routes_stream_escaped_string(conn, items[i].path) != 0
            || cs_routes_stream_literal(conn, "\",\"kind\":\"") != 0
            || cs_routes_stream_literal(conn, cs_upload_preview_kind_name(items[i].kind)) != 0
            || cs_routes_stream_literal(conn, "\"}") != 0) {
            return -1;
        }
    }

    return 0;
}

static int cs_upload_preview_write_response(struct mg_connection *conn, const cs_upload_preview_result *result) {
    if (!conn || !result) {
        return 1;
    }
    if (cs_routes_stream_begin_json_response(conn) != 0) {
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"ok\":true,\"overwriteableCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->overwriteable_count) != 0
        || cs_routes_stream_literal(conn, ",\"blockingCount\":") != 0
        || cs_routes_stream_unsigned(conn, result->blocking_count) != 0
        || cs_routes_stream_literal(conn, ",\"overwriteable\":[") != 0
        || cs_upload_preview_write_list(conn, result->overwriteable, result->overwriteable_preview_count) != 0
        || cs_routes_stream_literal(conn, "],\"blocking\":[") != 0
        || cs_upload_preview_write_list(conn, result->blocking, result->blocking_preview_count) != 0
        || cs_routes_stream_literal(conn, "]}") != 0
        || mg_send_chunk(conn, "", 0) < 0) {
        (void) mg_send_chunk(conn, "", 0);
    }

    return 1;
}

int cs_route_upload_preview_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_upload_request request_state;
    cs_upload_preview_result *preview_result = NULL;
    struct mg_form_data_handler form_handler;
    const struct mg_request_info *request = mg_get_request_info(conn);
    const char *cookie = mg_get_header(conn, "Cookie");
    int handled_fields;
    int response_status;
    size_t i;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }
    if (!request || request->content_length < 0 || request->content_length > cs_upload_request_limit_bytes()) {
        return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
    }
    preview_result = (cs_upload_preview_result *) calloc(1, sizeof(*preview_result));
    if (!preview_result) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }

    memset(&request_state, 0, sizeof(request_state));
    request_state.app = app;

    memset(&form_handler, 0, sizeof(form_handler));
    form_handler.field_found = cs_upload_preview_field_found;
    form_handler.field_get = cs_upload_preview_field_get;
    form_handler.field_store = cs_upload_preview_field_store;
    form_handler.user_data = &request_state;

    handled_fields = mg_handle_form_request(conn, &form_handler);
    if (handled_fields < 0 || request_state.failed
        || (request_state.preview_file_count == 0 && request_state.directory_count == 0)) {
        response_status = cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        free(preview_result);
        return response_status;
    }
    if (cs_prepare_upload_metadata(&request_state) != 0) {
        response_status = cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        free(preview_result);
        return response_status;
    }

    for (i = 0; i < request_state.directory_count; ++i) {
        if (cs_upload_preview_scan_directory(&request_state, request_state.directories[i], preview_result) != 0) {
            response_status = cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            free(preview_result);
            return response_status;
        }
    }
    for (i = 0; i < request_state.preview_file_count; ++i) {
        if (cs_upload_preview_scan_file(&request_state,
                                        request_state.preview_relative_dirs[i],
                                        request_state.preview_file_names[i],
                                        preview_result)
            != 0) {
            response_status = cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            free(preview_result);
            return response_status;
        }
    }

    response_status = cs_upload_preview_write_response(conn, preview_result);
    free(preview_result);
    return response_status;
}

int cs_route_upload_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_upload_request request_state;
    struct mg_form_data_handler form_handler;
    const struct mg_request_info *request = mg_get_request_info(conn);
    const char *cookie = mg_get_header(conn, "Cookie");
    int handled_fields;
    long long request_limit = cs_upload_request_limit_bytes();
    size_t i;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }
    if (!request || request->content_length < 0 || request->content_length > request_limit) {
        return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
    }
    if (cs_upload_prepare_temp_root(&app->paths) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"upload_prep_failed\"}");
    }

    memset(&request_state, 0, sizeof(request_state));
    request_state.app = app;

    memset(&form_handler, 0, sizeof(form_handler));
    form_handler.field_found = cs_upload_field_found;
    form_handler.field_get = cs_upload_field_get;
    form_handler.field_store = cs_upload_field_store;
    form_handler.user_data = &request_state;

    handled_fields = mg_handle_form_request(conn, &form_handler);
    if (handled_fields < 0 || request_state.failed
        || (request_state.plan_count == 0 && request_state.directory_count == 0)
        || request_state.stored_count != request_state.plan_count) {
        cs_upload_cleanup_temp_files(&request_state);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (cs_prepare_upload_metadata(&request_state) != 0) {
        cs_upload_cleanup_temp_files(&request_state);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    for (i = 0; i < request_state.plan_count; ++i) {
        if (request_state.file_sizes[i] > cs_upload_file_limit_bytes(&request_state)) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 413, "Payload Too Large", "{\"error\":\"upload_too_large\"}");
        }
    }

    for (i = 0; i < request_state.directory_count; ++i) {
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(request_state.path,
                                         request_state.directories[i],
                                         upload_dir,
                                         sizeof(upload_dir))
                != 0
            || upload_dir[0] == '\0') {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        }
        if (cs_upload_prepare_final_directory(request_state.final_root,
                                              request_state.app->paths.sdcard_root,
                                              upload_dir,
                                              request_state.path_flags)
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            if (errno == EEXIST || errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
                return cs_write_upload_errno_response(conn, upload_dir);
            }
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        }
    }

    for (i = 0; i < request_state.plan_count; ++i) {
        cs_upload_plan promoted_plan;
        char upload_dir[CS_PATH_MAX];

        if (cs_upload_join_relative_path(request_state.path,
                                         request_state.relative_dirs[i],
                                         upload_dir,
                                         sizeof(upload_dir))
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        }

        if (cs_upload_plan_make(&request_state.app->paths,
                                request_state.final_root,
                                request_state.app->paths.sdcard_root,
                                upload_dir,
                                request_state.file_names[i],
                                request_state.path_flags,
                                &promoted_plan)
            != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            if (errno == EEXIST || errno == EISDIR || errno == ENOTDIR || errno == EINVAL || errno == ENOTEMPTY) {
                char upload_path[CS_PATH_MAX];

                if (cs_upload_join_relative_path(upload_dir,
                                                 request_state.file_names[i],
                                                 upload_path,
                                                 sizeof(upload_path))
                    == 0) {
                    return cs_write_upload_errno_response(conn, upload_path);
                }
            }
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        }

        {
            int written = snprintf(promoted_plan.temp_path,
                                   sizeof(promoted_plan.temp_path),
                                   "%s",
                                   request_state.plans[i].temp_path);

            if (written < 0 || (size_t) written >= sizeof(promoted_plan.temp_path)) {
                cs_upload_cleanup_temp_files(&request_state);
                return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
            }
        }
        request_state.plans[i] = promoted_plan;
    }

    for (i = 0; i < request_state.plan_count; ++i) {
        char upload_path[CS_PATH_MAX];
        char combined_upload_path[CS_PATH_MAX];
        int promote_status;

        if (cs_upload_join_relative_path(request_state.relative_dirs[i],
                                         request_state.file_names[i],
                                         upload_path,
                                         sizeof(upload_path))
            != 0
            || cs_upload_join_relative_path(request_state.path,
                                            upload_path,
                                            combined_upload_path,
                                            sizeof(combined_upload_path))
                   != 0) {
            cs_upload_cleanup_temp_files(&request_state);
            return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
        }

        promote_status = request_state.overwrite_existing ? cs_upload_promote_replace(&request_state.plans[i])
                                                          : cs_upload_promote(&request_state.plans[i]);
        if (promote_status != 0) {
            size_t j;

            for (j = i; j < request_state.plan_count; ++j) {
                (void) remove(request_state.plans[j].temp_path);
            }
            return cs_write_upload_errno_response(conn, combined_upload_path);
        }
    }

    return cs_write_json(conn, 200, "OK", "{\"ok\":true}");
}
