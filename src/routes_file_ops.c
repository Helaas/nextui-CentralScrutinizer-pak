#include "cs_app.h"
#include "cs_file_ops.h"
#include "cs_library.h"
#include "cs_platforms.h"
#include "cs_server.h"
#include "cs_uploads.h"
#include "cs_util.h"

#include "civetweb.h"

#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct cs_replace_art_request {
    cs_app *app;
    char tag[64];
    char rom_path[CS_PATH_MAX];
    char temp_path[CS_PATH_MAX];
    int file_seen;
    int file_stored;
    int failed;
    int unsupported_type;
} cs_replace_art_request;

static atomic_ulong g_replace_art_nonce = ATOMIC_VAR_INIT(0);

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

static int cs_stream_begin_json_response(struct mg_connection *conn) {
    if (!conn) {
        return -1;
    }

    return mg_printf(conn,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     CS_SERVER_SECURITY_HEADERS_HTTP
                     "Cache-Control: no-store\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "\r\n")
                   <= 0
               ? -1
               : 0;
}

static int cs_stream_literal(struct mg_connection *conn, const char *literal) {
    size_t len;

    if (!conn || !literal) {
        return -1;
    }

    len = strlen(literal);
    if (len == 0) {
        return 0;
    }

    return mg_send_chunk(conn, literal, (unsigned int) len) < 0 ? -1 : 0;
}

static int cs_stream_escaped_string(struct mg_connection *conn, const char *value) {
    const unsigned char *cursor = (const unsigned char *) (value ? value : "");
    char out[512];
    size_t used = 0;

    while (*cursor != '\0') {
        const char *fragment = NULL;
        size_t fragment_len = 0;
        char escaped[8];

        switch (*cursor) {
            case '\\':
                fragment = "\\\\";
                fragment_len = 2;
                break;
            case '"':
                fragment = "\\\"";
                fragment_len = 2;
                break;
            case '\n':
                fragment = "\\n";
                fragment_len = 2;
                break;
            case '\r':
                fragment = "\\r";
                fragment_len = 2;
                break;
            case '\t':
                fragment = "\\t";
                fragment_len = 2;
                break;
            default:
                if (*cursor < 0x20) {
                    int n = snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int) *cursor);

                    if (n <= 0 || (size_t) n >= sizeof(escaped)) {
                        return -1;
                    }
                    fragment = escaped;
                    fragment_len = (size_t) n;
                } else {
                    escaped[0] = (char) *cursor;
                    escaped[1] = '\0';
                    fragment = escaped;
                    fragment_len = 1;
                }
                break;
        }

        if (used + fragment_len > sizeof(out)) {
            if (mg_send_chunk(conn, out, (unsigned int) used) < 0) {
                return -1;
            }
            used = 0;
        }

        memcpy(out + used, fragment, fragment_len);
        used += fragment_len;
        cursor += 1;
    }

    if (used > 0 && mg_send_chunk(conn, out, (unsigned int) used) < 0) {
        return -1;
    }

    return 0;
}

static int cs_route_guard_post(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");
    const char *csrf = mg_get_header(conn, "X-CS-CSRF");

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_csrf_is_valid(cookie, csrf)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

static int cs_route_guard_get(struct mg_connection *conn, void *cbdata, int allow_query_param) {
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, allow_query_param)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

static int cs_read_body(struct mg_connection *conn, char *buffer, size_t buffer_size, size_t *body_len_out) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    long long content_length;
    size_t received = 0;

    if (!request || !buffer || buffer_size == 0 || !body_len_out) {
        return -1;
    }

    content_length = request->content_length;
    if (content_length < 0 || (size_t) content_length >= buffer_size) {
        return -1;
    }

    while (received < (size_t) content_length) {
        int nread = mg_read(conn, buffer + received, (size_t) content_length - received);

        if (nread <= 0) {
            return -1;
        }
        received += (size_t) nread;
    }

    buffer[received] = '\0';
    *body_len_out = received;
    return 0;
}

static int cs_write_file_op_result(struct mg_connection *conn, const char *action) {
    char body[96];
    int written = snprintf(body, sizeof(body), "{\"ok\":true,\"action\":\"%s\"}", action ? action : "");

    if (written < 0 || (size_t) written >= sizeof(body)) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }

    return cs_write_json(conn, 200, "OK", body);
}

static int cs_write_errno_response(struct mg_connection *conn) {
    if (errno == ENOENT) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (errno == EEXIST || errno == ENOTEMPTY) {
        return cs_write_json(conn, 409, "Conflict", "{\"ok\":false}");
    }

    return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
}

typedef struct cs_file_search_match {
    char path[CS_PATH_MAX];
    char type[32];
} cs_file_search_match;

#define CS_FILE_SEARCH_MAX_RESULTS 200

static int cs_query_matches_name(const char *query, const char *name) {
    size_t query_len;
    size_t name_len;
    size_t i;
    size_t j;

    if (!query || !name) {
        return 0;
    }

    query_len = strlen(query);
    name_len = strlen(name);
    if (query_len == 0 || query_len > name_len) {
        return query_len == 0;
    }

    for (i = 0; i + query_len <= name_len; ++i) {
        for (j = 0; j < query_len; ++j) {
            if (tolower((unsigned char) name[i + j]) != tolower((unsigned char) query[j])) {
                break;
            }
        }
        if (j == query_len) {
            return 1;
        }
    }

    return 0;
}

static int cs_collect_file_search_matches(const char *absolute_path,
                                          const char *relative_path,
                                          const char *query,
                                          cs_file_search_match *matches,
                                          size_t *count,
                                          int *truncated) {
    DIR *dir;
    struct dirent *entry;

    if (!absolute_path || !relative_path || !query || !matches || !count || !truncated) {
        return -1;
    }

    dir = opendir(absolute_path);
    if (!dir) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child_absolute[CS_PATH_MAX];
        char child_relative[CS_PATH_MAX];
        struct stat st;
        int is_directory;

        if (*count >= CS_FILE_SEARCH_MAX_RESULTS) {
            *truncated = 1;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (CS_SAFE_SNPRINTF(child_absolute, sizeof(child_absolute), "%s/%s", absolute_path, entry->d_name) != 0) {
            continue;
        }
        if (relative_path[0] != '\0') {
            if (CS_SAFE_SNPRINTF(child_relative, sizeof(child_relative), "%s/%s", relative_path, entry->d_name) != 0) {
                continue;
            }
        } else if (CS_SAFE_SNPRINTF(child_relative, sizeof(child_relative), "%s", entry->d_name) != 0) {
            continue;
        }
        if (lstat(child_absolute, &st) != 0 || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
            continue;
        }

        is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
        if (cs_query_matches_name(query, entry->d_name)) {
            if (*count < CS_FILE_SEARCH_MAX_RESULTS) {
                if (CS_SAFE_SNPRINTF(matches[*count].path, sizeof(matches[*count].path), "%s", child_relative) != 0
                    || CS_SAFE_SNPRINTF(matches[*count].type,
                                        sizeof(matches[*count].type),
                                        "%s",
                                        is_directory ? "directory" : "file")
                           != 0) {
                    (void) closedir(dir);
                    return -1;
                }
                *count += 1;
            } else {
                *truncated = 1;
                break;
            }
        }
        if (is_directory && *count < CS_FILE_SEARCH_MAX_RESULTS
            && cs_collect_file_search_matches(child_absolute, child_relative, query, matches, count, truncated) != 0) {
            (void) closedir(dir);
            return -1;
        }
    }

    (void) closedir(dir);
    return 0;
}

#if defined(CS_TESTING)
int cs_file_search_collect_for_test(const char *absolute_path,
                                    const char *relative_path,
                                    const char *query,
                                    size_t *count_out,
                                    int *truncated_out) {
    cs_file_search_match matches[CS_FILE_SEARCH_MAX_RESULTS];
    size_t count = 0;
    int truncated = 0;
    int rc;

    if (count_out) {
        *count_out = 0;
    }
    if (truncated_out) {
        *truncated_out = 0;
    }

    rc = cs_collect_file_search_matches(absolute_path, relative_path, query, matches, &count, &truncated);
    if (rc == 0) {
        if (count_out) {
            *count_out = count;
        }
        if (truncated_out) {
            *truncated_out = truncated;
        }
    }

    return rc;
}
#endif

static unsigned long cs_replace_art_next_nonce(void) {
    return atomic_fetch_add_explicit(&g_replace_art_nonce, 1, memory_order_relaxed) + 1;
}

static int cs_filename_has_png_extension(const char *filename) {
    const char *ext;
    static const char png_ext[] = ".png";
    size_t i;

    if (!filename) {
        return 0;
    }

    ext = strrchr(filename, '.');
    if (!ext || strlen(ext) != sizeof(png_ext) - 1) {
        return 0;
    }

    for (i = 0; i < sizeof(png_ext) - 1; ++i) {
        if (tolower((unsigned char) ext[i]) != png_ext[i]) {
            return 0;
        }
    }

    return 1;
}

static int cs_split_relative_path(const char *relative_path,
                                  char *parent,
                                  size_t parent_size,
                                  char *name,
                                  size_t name_size) {
    const char *slash;
    size_t parent_len;

    if (!relative_path || !parent || parent_size == 0 || !name || name_size == 0) {
        return -1;
    }

    slash = strrchr(relative_path, '/');
    if (!slash) {
        if (snprintf(parent, parent_size, "%s", "") >= (int) parent_size
            || snprintf(name, name_size, "%s", relative_path) >= (int) name_size) {
            return -1;
        }
        return 0;
    }
    if (slash[1] == '\0') {
        return -1;
    }

    parent_len = (size_t) (slash - relative_path);
    if (parent_len >= parent_size) {
        return -1;
    }

    memcpy(parent, relative_path, parent_len);
    parent[parent_len] = '\0';
    return snprintf(name, name_size, "%s", slash + 1) < (int) name_size ? 0 : -1;
}

static int cs_build_art_relative_paths(const char *rom_relative_path,
                                       char *art_relative_path,
                                       size_t art_relative_path_size,
                                       char *art_base_path,
                                       size_t art_base_path_size) {
    const char *slash;
    const char *ext;
    size_t base_len;

    if (!rom_relative_path || rom_relative_path[0] == '\0' || !art_relative_path || art_relative_path_size == 0
        || !art_base_path || art_base_path_size == 0) {
        return -1;
    }

    slash = strrchr(rom_relative_path, '/');
    ext = strrchr(rom_relative_path, '.');
    if (ext && slash && ext < slash) {
        ext = NULL;
    }
    if (ext && ext == rom_relative_path) {
        ext = NULL;
    }
    base_len = ext ? (size_t) (ext - rom_relative_path) : strlen(rom_relative_path);
    if (base_len == 0) {
        return -1;
    }

    if (snprintf(art_base_path, art_base_path_size, ".media/%.*s", (int) base_len, rom_relative_path)
            >= (int) art_base_path_size
        || snprintf(art_relative_path, art_relative_path_size, "%s.png", art_base_path)
               >= (int) art_relative_path_size) {
        return -1;
    }

    return 0;
}

static void cs_remove_temp_upload(const char *path) {
    if (path && path[0] != '\0') {
        (void) remove(path);
    }
}

static int cs_cleanup_legacy_art_variants(const char *rom_root, const char *art_base_path) {
    static const char *extensions[] = {".jpg", ".jpeg", ".webp"};
    size_t i;

    if (!rom_root || !art_base_path) {
        return -1;
    }

    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        char candidate[CS_PATH_MAX];

        if (snprintf(candidate, sizeof(candidate), "%s%s", art_base_path, extensions[i]) >= (int) sizeof(candidate)) {
            return -1;
        }
        if (cs_safe_delete_under_root(rom_root, candidate) != 0 && errno != ENOENT) {
            return -1;
        }
    }

    return 0;
}

static int cs_replace_art_field_get(const char *key, const char *value, size_t valuelen, void *user_data) {
    cs_replace_art_request *state = (cs_replace_art_request *) user_data;
    char *target = NULL;
    size_t target_size = 0;
    size_t copy_len;

    if (!state || !key || !value) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    if (strcmp(key, "tag") == 0) {
        target = state->tag;
        target_size = sizeof(state->tag);
    } else if (strcmp(key, "path") == 0) {
        target = state->rom_path;
        target_size = sizeof(state->rom_path);
    }
    if (!target) {
        return MG_FORM_FIELD_HANDLE_NEXT;
    }

    copy_len = valuelen < target_size - 1 ? valuelen : target_size - 1;
    memcpy(target, value, copy_len);
    target[copy_len] = '\0';
    return MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_replace_art_field_found(const char *key,
                                      const char *filename,
                                      char *path,
                                      size_t pathlen,
                                      void *user_data) {
    cs_replace_art_request *state = (cs_replace_art_request *) user_data;
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
    if (state->file_seen) {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }
    if (!cs_filename_has_png_extension(filename)) {
        state->unsupported_type = 1;
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    written = snprintf(state->temp_path,
                       sizeof(state->temp_path),
                       "%s/.incoming-art-%ld-%lu.png",
                       state->app->paths.temp_upload_root,
                       (long) getpid(),
                       cs_replace_art_next_nonce());
    if (written < 0 || (size_t) written >= sizeof(state->temp_path)
        || snprintf(path, pathlen, "%s", state->temp_path) >= (int) pathlen) {
        state->failed = 1;
        return MG_FORM_FIELD_STORAGE_ABORT;
    }

    state->file_seen = 1;
    return MG_FORM_FIELD_STORAGE_STORE;
}

static int cs_replace_art_field_store(const char *path, long long file_size, void *user_data) {
    cs_replace_art_request *state = (cs_replace_art_request *) user_data;

    if (!state || !path || file_size < 0) {
        return MG_FORM_FIELD_HANDLE_ABORT;
    }
    if (strcmp(path, state->temp_path) != 0) {
        state->failed = 1;
        return MG_FORM_FIELD_HANDLE_ABORT;
    }

    state->file_stored = 1;
    return MG_FORM_FIELD_HANDLE_NEXT;
}

static int cs_resolve_scope_root(cs_app *app,
                                 const char *scope_value,
                                 const char *tag_value,
                                 char *root,
                                 size_t root_size,
                                 unsigned int *path_flags_out) {
    cs_browser_scope scope = cs_browser_scope_parse(scope_value);
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;

    if (!app || !scope_value || !root || root_size == 0 || !path_flags_out) {
        return -1;
    }
    if (scope == CS_SCOPE_INVALID) {
        return -1;
    }
    if (cs_browser_scope_requires_platform(scope)) {
        if (cs_platform_resolve(&app->paths, tag_value, &resolved_platform) != 0) {
            return -1;
        }
        platform = &resolved_platform;
    }
    if (cs_browser_root_for_scope(&app->paths, scope, platform, root, root_size) != 0) {
        return -1;
    }

    *path_flags_out = cs_browser_scope_allows_hidden(scope) ? CS_PATH_FLAG_ALLOW_HIDDEN : 0;
    return 0;
}

static int cs_route_rename_like(struct mg_connection *conn,
                                void *cbdata,
                                const char *from_key,
                                const char *to_key,
                                const char *action) {
    cs_app *app = (cs_app *) cbdata;
    char body[4096];
    char scope_value[32];
    char tag_value[64];
    char from_relative[CS_PATH_MAX];
    char to_relative[CS_PATH_MAX];
    char root[CS_PATH_MAX];
    size_t body_len = 0;
    unsigned int path_flags = 0;
    int guard_status = cs_route_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (cs_read_body(conn, body, sizeof(body), &body_len) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (mg_get_var(body, body_len, "scope", scope_value, sizeof(scope_value)) <= 0
        || mg_get_var(body, body_len, from_key, from_relative, sizeof(from_relative)) <= 0
        || mg_get_var(body, body_len, to_key, to_relative, sizeof(to_relative)) <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    (void) mg_get_var(body, body_len, "tag", tag_value, sizeof(tag_value));
    if (cs_resolve_scope_root(app, scope_value, tag_value, root, sizeof(root), &path_flags) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_safe_rename_under_root_with_flags(root, from_relative, to_relative, path_flags) != 0) {
        return cs_write_errno_response(conn);
    }

    return cs_write_file_op_result(conn, action);
}

int cs_route_rename_handler(struct mg_connection *conn, void *cbdata) {
    return cs_route_rename_like(conn, cbdata, "from", "to", "rename");
}

int cs_route_delete_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[4096];
    char scope_value[32];
    char tag_value[64];
    char relative_path[CS_PATH_MAX];
    char root[CS_PATH_MAX];
    size_t body_len = 0;
    unsigned int path_flags = 0;
    int guard_status = cs_route_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (cs_read_body(conn, body, sizeof(body), &body_len) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (mg_get_var(body, body_len, "scope", scope_value, sizeof(scope_value)) <= 0
        || mg_get_var(body, body_len, "path", relative_path, sizeof(relative_path)) <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    (void) mg_get_var(body, body_len, "tag", tag_value, sizeof(tag_value));
    if (cs_resolve_scope_root(app, scope_value, tag_value, root, sizeof(root), &path_flags) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_safe_delete_under_root_with_flags(root, relative_path, path_flags) != 0) {
        return cs_write_errno_response(conn);
    }

    return cs_write_file_op_result(conn, "delete");
}

int cs_route_create_folder_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[4096];
    char scope_value[32];
    char tag_value[64];
    char relative_path[CS_PATH_MAX];
    char root[CS_PATH_MAX];
    size_t body_len = 0;
    unsigned int path_flags = 0;
    int guard_status = cs_route_guard_post(conn, cbdata);

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (cs_read_body(conn, body, sizeof(body), &body_len) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (mg_get_var(body, body_len, "scope", scope_value, sizeof(scope_value)) <= 0
        || mg_get_var(body, body_len, "path", relative_path, sizeof(relative_path)) <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    (void) mg_get_var(body, body_len, "tag", tag_value, sizeof(tag_value));
    if (cs_resolve_scope_root(app, scope_value, tag_value, root, sizeof(root), &path_flags) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_safe_create_directory_under_root_with_flags(root, relative_path, path_flags) != 0) {
        return cs_write_errno_response(conn);
    }

    return cs_write_file_op_result(conn, "create-folder");
}

int cs_route_download_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char scope_value[32];
    char tag_value[64];
    char relative_path[CS_PATH_MAX];
    char root[CS_PATH_MAX];
    char absolute_path[CS_PATH_MAX];
    const char *filename;
    FILE *file = NULL;
    unsigned int path_flags = 0;
    struct stat st;
    char buffer[4096];
    size_t nread;
    int guard_status = cs_route_guard_get(conn, cbdata, 1);

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (guard_status != 0) {
        return guard_status;
    }
    if (!request || !request->query_string) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (mg_get_var(request->query_string, strlen(request->query_string), "scope", scope_value, sizeof(scope_value))
            <= 0
        || mg_get_var(request->query_string, strlen(request->query_string), "path", relative_path, sizeof(relative_path))
               <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    (void) mg_get_var(request->query_string, strlen(request->query_string), "tag", tag_value, sizeof(tag_value));
    if (cs_resolve_scope_root(app, scope_value, tag_value, root, sizeof(root), &path_flags) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_resolve_path_under_root_with_flags(root, relative_path, path_flags, absolute_path, sizeof(absolute_path))
        != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (lstat(absolute_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }

    filename = strrchr(relative_path, '/');
    filename = filename ? filename + 1 : relative_path;
    file = fopen(absolute_path, "rb");
    if (!file) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/octet-stream\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %lld\r\n"
              "Content-Disposition: attachment; filename=\"%s\"\r\n"
              "\r\n",
              (long long) st.st_size,
              filename);

    while ((nread = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (mg_write(conn, buffer, nread) < 0) {
            break;
        }
    }
    fclose(file);
    return 1;
}

int cs_route_file_search_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char relative_path[CS_PATH_MAX];
    char query_value[256];
    char absolute_path[CS_PATH_MAX];
    cs_file_search_match matches[CS_FILE_SEARCH_MAX_RESULTS];
    size_t match_count = 0;
    int truncated = 0;
    struct stat st;
    size_t i;
    int guard_status = cs_route_guard_get(conn, cbdata, 0);

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (guard_status != 0) {
        return guard_status;
    }
    if (!request || !request->query_string) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_query\"}");
    }

    memset(relative_path, 0, sizeof(relative_path));
    memset(query_value, 0, sizeof(query_value));
    (void) mg_get_var(request->query_string, strlen(request->query_string), "path", relative_path, sizeof(relative_path));
    if (mg_get_var(request->query_string, strlen(request->query_string), "q", query_value, sizeof(query_value)) <= 0
        || query_value[0] == '\0') {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_query\"}");
    }
    if (cs_resolve_path_under_root_with_flags(app->paths.sdcard_root,
                                              relative_path,
                                              CS_PATH_FLAG_ALLOW_EMPTY | CS_PATH_FLAG_ALLOW_HIDDEN,
                                              absolute_path,
                                              sizeof(absolute_path))
        != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_path\"}");
    }
    if (lstat(absolute_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return cs_write_json(conn, 404, "Not Found", "{\"error\":\"path_not_found\"}");
    }
    if (cs_collect_file_search_matches(absolute_path, relative_path, query_value, matches, &match_count, &truncated) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"search_failed\"}");
    }

    if (cs_stream_begin_json_response(conn) != 0) {
        return 1;
    }
    if (cs_stream_literal(conn, "{\"basePath\":\"") != 0
        || cs_stream_escaped_string(conn, relative_path) != 0
        || cs_stream_literal(conn, "\",\"query\":\"") != 0
        || cs_stream_escaped_string(conn, query_value) != 0
        || cs_stream_literal(conn, "\",\"results\":[") != 0) {
        goto stream_fail;
    }

    for (i = 0; i < match_count; ++i) {
        if (i > 0 && cs_stream_literal(conn, ",") != 0) {
            goto stream_fail;
        }
        if (cs_stream_literal(conn, "{\"path\":\"") != 0
            || cs_stream_escaped_string(conn, matches[i].path) != 0
            || cs_stream_literal(conn, "\",\"type\":\"") != 0
            || cs_stream_escaped_string(conn, matches[i].type) != 0
            || cs_stream_literal(conn, "\"}") != 0) {
            goto stream_fail;
        }
    }
    if (cs_stream_literal(conn, "],\"truncated\":") != 0
        || cs_stream_literal(conn, truncated ? "true" : "false") != 0
        || cs_stream_literal(conn, "}") != 0
        || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    return 1;

stream_fail:
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}

int cs_route_replace_art_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_replace_art_request request_state;
    struct mg_form_data_handler form_handler;
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;
    char rom_root[CS_PATH_MAX];
    char rom_absolute[CS_PATH_MAX];
    char art_relative_path[CS_PATH_MAX];
    char art_base_path[CS_PATH_MAX];
    char art_dir[CS_PATH_MAX];
    char art_name[256];
    cs_upload_plan plan;
    struct stat st;
    int guard_status = cs_route_guard_post(conn, cbdata);
    int handled_fields;

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (cs_upload_prepare_temp_root(&app->paths) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"upload_prep_failed\"}");
    }

    memset(&request_state, 0, sizeof(request_state));
    request_state.app = app;
    memset(&form_handler, 0, sizeof(form_handler));
    form_handler.field_found = cs_replace_art_field_found;
    form_handler.field_get = cs_replace_art_field_get;
    form_handler.field_store = cs_replace_art_field_store;
    form_handler.user_data = &request_state;

    handled_fields = mg_handle_form_request(conn, &form_handler);
    if (handled_fields < 0 || request_state.failed || !request_state.file_seen || !request_state.file_stored) {
        cs_remove_temp_upload(request_state.temp_path);
        if (request_state.unsupported_type) {
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false,\"error\":\"unsupported_art_type\"}");
        }
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (request_state.tag[0] == '\0' || request_state.rom_path[0] == '\0') {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }

    if (cs_platform_resolve(&app->paths, request_state.tag, &resolved_platform) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    platform = &resolved_platform;
    if (cs_browser_root_for_scope(&app->paths, CS_SCOPE_ROMS, platform, rom_root, sizeof(rom_root)) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_resolve_path_under_root(rom_root, request_state.rom_path, rom_absolute, sizeof(rom_absolute)) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (lstat(rom_absolute, &st) != 0 || !S_ISREG(st.st_mode)) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_build_art_relative_paths(request_state.rom_path,
                                    art_relative_path,
                                    sizeof(art_relative_path),
                                    art_base_path,
                                    sizeof(art_base_path))
        != 0
        || cs_split_relative_path(art_relative_path, art_dir, sizeof(art_dir), art_name, sizeof(art_name)) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (cs_upload_plan_make(&app->paths, rom_root, app->paths.sdcard_root, art_dir, art_name, 0, &plan) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (snprintf(plan.temp_path, sizeof(plan.temp_path), "%s", request_state.temp_path) >= (int) sizeof(plan.temp_path)) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }
    if (cs_upload_promote_replace(&plan) != 0) {
        cs_remove_temp_upload(request_state.temp_path);
        return cs_write_errno_response(conn);
    }
    if (cs_cleanup_legacy_art_variants(rom_root, art_base_path) != 0) {
        return cs_write_errno_response(conn);
    }

    return cs_write_file_op_result(conn, "replace-art");
}

#define CS_WRITE_MAX_BYTES (1U << 20)

int cs_route_write_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char scope_value[32];
    char tag_value[64];
    char relative_path[CS_PATH_MAX];
    char root[CS_PATH_MAX];
    unsigned int path_flags = 0;
    long long content_length;
    char *body = NULL;
    size_t received = 0;
    int guard_status = cs_route_guard_post(conn, cbdata);
    int rc;

    if (guard_status != 0) {
        return guard_status;
    }
    if (!app || !request) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!request->query_string
        || mg_get_var(request->query_string,
                      strlen(request->query_string),
                      "scope",
                      scope_value,
                      sizeof(scope_value))
               <= 0
        || mg_get_var(request->query_string,
                      strlen(request->query_string),
                      "path",
                      relative_path,
                      sizeof(relative_path))
               <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    (void) mg_get_var(request->query_string, strlen(request->query_string), "tag", tag_value, sizeof(tag_value));

    content_length = request->content_length;
    if (content_length < 0 || (unsigned long long) content_length > CS_WRITE_MAX_BYTES) {
        return cs_write_json(conn, 413, "Payload Too Large", "{\"ok\":false}");
    }
    if (cs_resolve_scope_root(app, scope_value, tag_value, root, sizeof(root), &path_flags) != 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }

    body = (char *) malloc((size_t) content_length + 1);
    if (!body) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }
    while (received < (size_t) content_length) {
        int nread = mg_read(conn, body + received, (size_t) content_length - received);

        if (nread <= 0) {
            free(body);
            return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
        }
        received += (size_t) nread;
    }
    body[received] = '\0';

    rc = cs_safe_write_under_root_with_flags(root, relative_path, body, received, path_flags);
    free(body);
    if (rc != 0) {
        return cs_write_errno_response(conn);
    }

    return cs_write_file_op_result(conn, "write");
}
