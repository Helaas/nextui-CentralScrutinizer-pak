#include "cs_app.h"
#include "cs_library.h"
#include "cs_platforms.h"
#include "cs_server.h"
#include "cs_states.h"

#include "civetweb.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cs_write_json(struct mg_connection *conn, int status, const char *reason, const char *body);

static int cs_route_guard_get(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

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

static int cs_stream_unsigned(struct mg_connection *conn, unsigned long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%llu", value) < 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_stream_signed(struct mg_connection *conn, long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%lld", value) < 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_stream_browser_entry(struct mg_connection *conn, const cs_browser_entry *entry, int *first_entry) {
    if (!conn || !entry || !first_entry) {
        return -1;
    }
    if (!*first_entry && cs_stream_literal(conn, ",") != 0) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"name\":\"") != 0
        || cs_stream_escaped_string(conn, entry->name) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, entry->path) != 0
        || cs_stream_literal(conn, "\",\"type\":\"") != 0
        || cs_stream_escaped_string(conn, entry->type) != 0
        || cs_stream_literal(conn, "\",\"size\":") != 0
        || cs_stream_unsigned(conn, entry->size) != 0
        || cs_stream_literal(conn, ",\"modified\":") != 0
        || cs_stream_signed(conn, entry->modified) != 0
        || cs_stream_literal(conn, ",\"status\":\"") != 0
        || cs_stream_escaped_string(conn, entry->status) != 0
        || cs_stream_literal(conn, "\",\"thumbnailPath\":\"") != 0
        || cs_stream_escaped_string(conn, entry->thumbnail_path) != 0
        || cs_stream_literal(conn, "\"}") != 0) {
        return -1;
    }

    *first_entry = 0;
    return 0;
}

static int cs_stream_breadcrumb(struct mg_connection *conn,
                                const cs_browser_breadcrumb *breadcrumb,
                                int *first_entry) {
    if (!conn || !breadcrumb || !first_entry) {
        return -1;
    }
    if (!*first_entry && cs_stream_literal(conn, ",") != 0) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"label\":\"") != 0
        || cs_stream_escaped_string(conn, breadcrumb->label) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, breadcrumb->path) != 0
        || cs_stream_literal(conn, "\"}") != 0) {
        return -1;
    }

    *first_entry = 0;
    return 0;
}

static int cs_count_files_recursive(const char *path, int allow_hidden, int skip_media_dir) {
    DIR *dir;
    struct dirent *entry;
    int total = 0;

    if (!path) {
        return 0;
    }
    dir = opendir(path);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char child[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!allow_hidden && entry->d_name[0] == '.') {
            continue;
        }
        if (skip_media_dir && strcmp(entry->d_name, ".media") == 0) {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) < 0) {
            continue;
        }
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            total += cs_count_files_recursive(child, allow_hidden, skip_media_dir);
        } else if (S_ISREG(st.st_mode)) {
            total += 1;
        }
    }

    closedir(dir);
    return total;
}

static int cs_stream_platform_object(struct mg_connection *conn,
                                     const cs_paths *paths,
                                     const cs_platform_info *platform,
                                     int *first_platform) {
    char rom_root[CS_PATH_MAX];
    char save_root[CS_PATH_MAX];
    char bios_root[CS_PATH_MAX];
    char overlays_root[CS_PATH_MAX];
    char cheats_root[CS_PATH_MAX];
    size_t state_count = 0;
    int rom_count = 0;
    int save_count = 0;
    int bios_count = 0;
    int overlay_count = 0;
    int cheat_count = 0;

    if (!conn || !paths || !platform || !first_platform) {
        return -1;
    }
    if (cs_browser_root_for_scope(paths, CS_SCOPE_ROMS, platform, rom_root, sizeof(rom_root)) == 0) {
        rom_count = cs_count_files_recursive(rom_root, 0, 1);
    }
    if (cs_browser_root_for_scope(paths, CS_SCOPE_SAVES, platform, save_root, sizeof(save_root)) == 0) {
        save_count = cs_count_files_recursive(save_root, 0, 0);
    }
    if (cs_states_collect(paths, platform, NULL, 0, &state_count, NULL) != 0) {
        state_count = 0;
    }
    if (cs_browser_root_for_scope(paths, CS_SCOPE_BIOS, platform, bios_root, sizeof(bios_root)) == 0) {
        bios_count = cs_count_files_recursive(bios_root, 0, 0);
    }
    if (cs_browser_root_for_scope(paths, CS_SCOPE_OVERLAYS, platform, overlays_root, sizeof(overlays_root)) == 0) {
        overlay_count = cs_count_files_recursive(overlays_root, 0, 0);
    }
    if (cs_browser_root_for_scope(paths, CS_SCOPE_CHEATS, platform, cheats_root, sizeof(cheats_root)) == 0) {
        cheat_count = cs_count_files_recursive(cheats_root, 0, 0);
    }

    if (!*first_platform && cs_stream_literal(conn, ",") != 0) {
        return -1;
    }
    if (cs_stream_literal(conn, "{\"tag\":\"") != 0
        || cs_stream_escaped_string(conn, platform->tag) != 0
        || cs_stream_literal(conn, "\",\"name\":\"") != 0
        || cs_stream_escaped_string(conn, platform->name) != 0
        || cs_stream_literal(conn, "\",\"group\":\"") != 0
        || cs_stream_escaped_string(conn, platform->group) != 0
        || cs_stream_literal(conn, "\",\"icon\":\"") != 0
        || cs_stream_escaped_string(conn, platform->icon) != 0
        || cs_stream_literal(conn, "\",\"isCustom\":") != 0
        || cs_stream_literal(conn, platform->is_custom ? "true" : "false") != 0
        || cs_stream_literal(conn, ",\"romPath\":\"Roms/") != 0
        || cs_stream_escaped_string(conn, platform->rom_directory) != 0
        || cs_stream_literal(conn, "\",\"savePath\":\"Saves/") != 0
        || cs_stream_escaped_string(conn, platform->primary_code) != 0
        || cs_stream_literal(conn, "\",\"biosPath\":\"Bios/") != 0
        || cs_stream_escaped_string(conn, platform->primary_code) != 0
        || cs_stream_literal(conn, "\",\"counts\":{\"roms\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) rom_count) != 0
        || cs_stream_literal(conn, ",\"saves\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) save_count) != 0
        || cs_stream_literal(conn, ",\"states\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) state_count) != 0
        || cs_stream_literal(conn, ",\"bios\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) bios_count) != 0
        || cs_stream_literal(conn, ",\"overlays\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) overlay_count) != 0
        || cs_stream_literal(conn, ",\"cheats\":") != 0
        || cs_stream_unsigned(conn, (unsigned long long) cheat_count) != 0
        || cs_stream_literal(conn, "}}") != 0) {
        return -1;
    }

    *first_platform = 0;
    return 0;
}

int cs_route_platforms_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_platform_info platforms[256];
    size_t platform_count = 0;
    size_t i;
    const char *current_group = NULL;
    int first_group = 1;
    int first_platform = 1;
    int guard_status;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_route_guard_get(conn, cbdata);
    if (guard_status != 0) {
        return guard_status;
    }
    if (cs_stream_begin_json_response(conn) != 0) {
        return 1;
    }
    if (cs_stream_literal(conn, "{\"groups\":[") != 0) {
        goto stream_fail;
    }

    if (cs_platform_discover(&app->paths, platforms, sizeof(platforms) / sizeof(platforms[0]), &platform_count) != 0) {
        goto stream_fail;
    }

    for (i = 0; i < platform_count; ++i) {
        const cs_platform_info *platform = &platforms[i];

        if (!current_group || strcmp(current_group, platform->group) != 0) {
            if (current_group) {
                if (cs_stream_literal(conn, "]}") != 0) {
                    goto stream_fail;
                }
            }
            if (!first_group && cs_stream_literal(conn, ",") != 0) {
                goto stream_fail;
            }
            if (cs_stream_literal(conn, "{\"name\":\"") != 0
                || cs_stream_escaped_string(conn, platform->group) != 0
                || cs_stream_literal(conn, "\",\"platforms\":[") != 0) {
                goto stream_fail;
            }
            current_group = platform->group;
            first_group = 0;
            first_platform = 1;
        }

        if (cs_stream_platform_object(conn, &app->paths, platform, &first_platform) != 0) {
            goto stream_fail;
        }
    }

    if (current_group && cs_stream_literal(conn, "]}") != 0) {
        goto stream_fail;
    }
    if (cs_stream_literal(conn, "]}") != 0 || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    return 1;

stream_fail:
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}

int cs_route_browser_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char scope_value[32];
    char tag_value[64];
    char path_value[CS_PATH_MAX];
    cs_browser_scope scope;
    cs_platform_info resolved_platform = {0};
    const cs_platform_info *platform = NULL;
    cs_browser_result *result = NULL;
    size_t i;
    int first_entry = 1;
    int guard_status;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_route_guard_get(conn, cbdata);
    if (guard_status != 0) {
        return guard_status;
    }

    memset(scope_value, 0, sizeof(scope_value));
    memset(tag_value, 0, sizeof(tag_value));
    memset(path_value, 0, sizeof(path_value));

    if (!request || !request->query_string || request->query_string[0] == '\0') {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_scope\"}");
    }
    if (mg_get_var(request->query_string, strlen(request->query_string), "scope", scope_value, sizeof(scope_value))
        <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_scope\"}");
    }
    scope = cs_browser_scope_parse(scope_value);
    if (scope == CS_SCOPE_INVALID) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"invalid_scope\"}");
    }

    (void) mg_get_var(request->query_string, strlen(request->query_string), "tag", tag_value, sizeof(tag_value));
    (void) mg_get_var(request->query_string, strlen(request->query_string), "path", path_value, sizeof(path_value));

    if (cs_browser_scope_requires_platform(scope)) {
        if (cs_platform_resolve(&app->paths, tag_value, &resolved_platform) != 0) {
            return cs_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
        }
        platform = &resolved_platform;
    }

    result = (cs_browser_result *) calloc(1, sizeof(*result));
    if (!result) {
        free(result);
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"alloc_failed\"}");
    }

    if (cs_browser_list(&app->paths, scope, platform, path_value, result) != 0) {
        free(result);
        return cs_write_json(conn, 404, "Not Found", "{\"error\":\"path_not_found\"}");
    }

    if (cs_stream_begin_json_response(conn) != 0) {
        free(result);
        return 1;
    }
    if (cs_stream_literal(conn, "{\"scope\":\"") != 0
        || cs_stream_escaped_string(conn, result->scope) != 0
        || cs_stream_literal(conn, "\",\"title\":\"") != 0
        || cs_stream_escaped_string(conn, result->title) != 0
        || cs_stream_literal(conn, "\",\"rootPath\":\"") != 0
        || cs_stream_escaped_string(conn, result->root_path) != 0
        || cs_stream_literal(conn, "\",\"path\":\"") != 0
        || cs_stream_escaped_string(conn, result->path) != 0
        || cs_stream_literal(conn, "\",\"breadcrumbs\":[") != 0) {
        goto stream_fail;
    }

    first_entry = 1;
    for (i = 0; i < result->breadcrumb_count; ++i) {
        if (cs_stream_breadcrumb(conn, &result->breadcrumbs[i], &first_entry) != 0) {
            goto stream_fail;
        }
    }
    if (cs_stream_literal(conn, "],\"entries\":[") != 0) {
        goto stream_fail;
    }

    first_entry = 1;
    for (i = 0; i < result->count; ++i) {
        if (cs_stream_browser_entry(conn, &result->entries[i], &first_entry) != 0) {
            goto stream_fail;
        }
    }

    if (cs_stream_literal(conn, "],\"truncated\":") != 0
        || cs_stream_literal(conn, result->truncated ? "true" : "false") != 0) {
        goto stream_fail;
    }
    if (cs_stream_literal(conn, "}") != 0 || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    free(result);
    return 1;

stream_fail:
    free(result);
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}
