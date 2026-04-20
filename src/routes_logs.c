#include "cs_app.h"
#include "cs_file_ops.h"
#include "cs_server.h"
#include "cs_util.h"

#include "civetweb.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CS_LOGS_MAX_FILES 256
#define CS_LOGS_TAIL_LINES 200
#define CS_LOGS_TAIL_WINDOW (128 * 1024)
#define CS_LOGS_TAIL_MAX_STREAM_SECONDS 600
#define CS_LOGS_TAIL_IDLE_SECONDS 60
#define CS_LOGS_TAIL_MAX_CONCURRENT 4

static atomic_int g_active_log_tails = ATOMIC_VAR_INIT(0);

typedef struct cs_log_file {
    char path[CS_PATH_MAX];
    unsigned long long size;
    long long modified;
} cs_log_file;

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
                    if (CS_SAFE_SNPRINTF(escaped, sizeof(escaped), "\\u%04x", (unsigned int) *cursor) != 0) {
                        return -1;
                    }
                    fragment = escaped;
                    fragment_len = strlen(escaped);
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

    if (CS_SAFE_SNPRINTF(buffer, sizeof(buffer), "%llu", value) != 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_stream_signed(struct mg_connection *conn, long long value) {
    char buffer[64];

    if (CS_SAFE_SNPRINTF(buffer, sizeof(buffer), "%lld", value) != 0) {
        return -1;
    }

    return cs_stream_literal(conn, buffer);
}

static int cs_logs_guard_get(struct mg_connection *conn, void *cbdata, int allow_query_param) {
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, allow_query_param)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}

static int cs_logs_tail_try_acquire(void) {
    int current = atomic_load_explicit(&g_active_log_tails, memory_order_relaxed);

    while (current < CS_LOGS_TAIL_MAX_CONCURRENT) {
        if (atomic_compare_exchange_weak_explicit(&g_active_log_tails,
                                                  &current,
                                                  current + 1,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return 0;
        }
    }

    return -1;
}

static void cs_logs_tail_release(void) {
    (void) atomic_fetch_sub_explicit(&g_active_log_tails, 1, memory_order_relaxed);
}

static int cs_logs_make_root(const cs_app *app, char *buffer, size_t buffer_len) {
    if (!app || !buffer || buffer_len == 0) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s/.userdata", app->paths.sdcard_root);
}

static int cs_logs_has_extension(const char *name, const char *extension) {
    size_t name_len;
    size_t ext_len;

    if (!name || !extension) {
        return 0;
    }

    name_len = strlen(name);
    ext_len = strlen(extension);
    return name_len >= ext_len && strcmp(name + name_len - ext_len, extension) == 0;
}

static int cs_logs_is_candidate(const char *relative_path, const char *name) {
    if (!relative_path || !name) {
        return 0;
    }

    return strstr(relative_path, "/logs/") != NULL || strncmp(relative_path, "logs/", 5) == 0
           || cs_logs_has_extension(name, ".log") || cs_logs_has_extension(name, ".txt")
           || cs_logs_has_extension(name, ".out");
}

static int cs_logs_scan_directory(const char *root,
                                  const char *relative_path,
                                  cs_log_file *files,
                                  size_t *count) {
    char absolute[CS_PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if (!root || !files || !count) {
        return -1;
    }

    if (relative_path && relative_path[0] != '\0') {
        if (CS_SAFE_SNPRINTF(absolute, sizeof(absolute), "%s/%s", root, relative_path) != 0) {
            return -1;
        }
    } else if (CS_SAFE_SNPRINTF(absolute, sizeof(absolute), "%s", root) != 0) {
        return -1;
    }

    dir = opendir(absolute);
    if (!dir) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char next_relative[CS_PATH_MAX];
        char next_absolute[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (relative_path && relative_path[0] != '\0') {
            if (CS_SAFE_SNPRINTF(next_relative, sizeof(next_relative), "%s/%s", relative_path, entry->d_name) != 0
                || CS_SAFE_SNPRINTF(next_absolute, sizeof(next_absolute), "%s/%s", root, next_relative) != 0) {
                continue;
            }
        } else if (CS_SAFE_SNPRINTF(next_relative, sizeof(next_relative), "%s", entry->d_name) != 0
                   || CS_SAFE_SNPRINTF(next_absolute, sizeof(next_absolute), "%s/%s", root, entry->d_name) != 0) {
            continue;
        }

        if (lstat(next_absolute, &st) != 0 || S_ISLNK(st.st_mode)) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            (void) cs_logs_scan_directory(root, next_relative, files, count);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !cs_logs_is_candidate(next_relative, entry->d_name) || *count >= CS_LOGS_MAX_FILES) {
            continue;
        }

        if (CS_SAFE_SNPRINTF(files[*count].path, sizeof(files[*count].path), "%s", next_relative) != 0) {
            continue;
        }
        files[*count].size = (unsigned long long) st.st_size;
        files[*count].modified = (long long) st.st_mtime;
        *count += 1;
    }

    (void) closedir(dir);
    return 0;
}

static int cs_logs_compare_descending(const void *left, const void *right) {
    const cs_log_file *a = (const cs_log_file *) left;
    const cs_log_file *b = (const cs_log_file *) right;

    if (a->modified < b->modified) {
        return 1;
    }
    if (a->modified > b->modified) {
        return -1;
    }

    return strcmp(a->path, b->path);
}

int cs_route_logs_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_log_file files[CS_LOGS_MAX_FILES];
    char root[CS_PATH_MAX];
    size_t count = 0;
    size_t i;
    int first = 1;
    int guard_status;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_logs_guard_get(conn, cbdata, 0);
    if (guard_status != 0) {
        return guard_status;
    }
    if (cs_logs_make_root(app, root, sizeof(root)) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_root\"}");
    }

    memset(files, 0, sizeof(files));
    (void) cs_logs_scan_directory(root, "", files, &count);
    qsort(files, count, sizeof(files[0]), cs_logs_compare_descending);

    if (cs_stream_begin_json_response(conn) != 0
        || cs_stream_literal(conn, "{\"root\":\".userdata\",\"files\":[") != 0) {
        return 1;
    }

    for (i = 0; i < count; ++i) {
        if (!first && cs_stream_literal(conn, ",") != 0) {
            return 1;
        }
        if (cs_stream_literal(conn, "{\"path\":\"") != 0
            || cs_stream_escaped_string(conn, files[i].path) != 0
            || cs_stream_literal(conn, "\",\"size\":") != 0
            || cs_stream_unsigned(conn, files[i].size) != 0
            || cs_stream_literal(conn, ",\"modified\":") != 0
            || cs_stream_signed(conn, files[i].modified) != 0
            || cs_stream_literal(conn, "}") != 0) {
            return 1;
        }
        first = 0;
    }

    (void) cs_stream_literal(conn, "]}");
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}

int cs_route_logs_download_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char root[CS_PATH_MAX];
    char relative_path[CS_PATH_MAX];
    char absolute_path[CS_PATH_MAX];
    const char *filename;
    FILE *file = NULL;
    struct stat st;
    char buffer[4096];
    size_t nread;
    int guard_status;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_logs_guard_get(conn, cbdata, 1);
    if (guard_status != 0) {
        return guard_status;
    }
    if (!request || !request->query_string
        || cs_logs_make_root(app, root, sizeof(root)) != 0
        || mg_get_var(request->query_string, strlen(request->query_string), "path", relative_path, sizeof(relative_path))
               <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (cs_resolve_path_under_root_with_flags(root,
                                              relative_path,
                                              CS_PATH_FLAG_ALLOW_HIDDEN,
                                              absolute_path,
                                              sizeof(absolute_path))
        != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (lstat(absolute_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }

    file = fopen(absolute_path, "rb");
    if (!file) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }

    filename = strrchr(relative_path, '/');
    filename = filename ? filename + 1 : relative_path;
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

static int cs_logs_send_recent_lines(struct mg_connection *conn, int fd) {
    struct stat st;
    off_t start = 0;
    char *buffer;
    ssize_t nread;
    size_t used;
    size_t start_index = 0;
    int line_count = 0;
    size_t i;

    if (!conn || fd < 0 || fstat(fd, &st) != 0) {
        return -1;
    }

    if (st.st_size > CS_LOGS_TAIL_WINDOW) {
        start = st.st_size - CS_LOGS_TAIL_WINDOW;
    }
    if (lseek(fd, start, SEEK_SET) < 0) {
        return -1;
    }

    buffer = (char *) malloc((size_t) (st.st_size - start) + 1u);
    if (!buffer) {
        return -1;
    }

    used = 0;
    while ((nread = read(fd, buffer + used, (size_t) (st.st_size - start) - used)) > 0) {
        used += (size_t) nread;
    }
    if (nread < 0) {
        free(buffer);
        return -1;
    }

    for (i = used; i > 0; --i) {
        if (buffer[i - 1] == '\n') {
            line_count += 1;
            if (line_count > CS_LOGS_TAIL_LINES) {
                start_index = i;
                break;
            }
        }
    }

    if (used > start_index && mg_send_chunk(conn, buffer + start_index, (unsigned int) (used - start_index)) < 0) {
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
}

int cs_route_logs_tail_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char root[CS_PATH_MAX];
    char relative_path[CS_PATH_MAX];
    char absolute_path[CS_PATH_MAX];
    int fd = -1;
    off_t offset = 0;
    char buffer[4096];
    int guard_status;
    int tail_slot_acquired = 0;
    time_t started_at;
    time_t last_sent_at;

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_logs_guard_get(conn, cbdata, 0);
    if (guard_status != 0) {
        return guard_status;
    }
    if (!request || !request->query_string
        || cs_logs_make_root(app, root, sizeof(root)) != 0
        || mg_get_var(request->query_string, strlen(request->query_string), "path", relative_path, sizeof(relative_path))
               <= 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    if (cs_resolve_path_under_root_with_flags(root,
                                              relative_path,
                                              CS_PATH_FLAG_ALLOW_HIDDEN,
                                              absolute_path,
                                              sizeof(absolute_path))
        != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }

    fd = open(absolute_path, O_RDONLY);
    if (fd < 0) {
        return cs_write_json(conn, 404, "Not Found", "{\"ok\":false}");
    }
    if (cs_logs_tail_try_acquire() != 0) {
        close(fd);
        return cs_write_json(conn, 429, "Too Many Requests", "{\"error\":\"too_many_tails\"}");
    }
    tail_slot_acquired = 1;

    if (mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/plain; charset=utf-8\r\n"
                  CS_SERVER_SECURITY_HEADERS_HTTP
                  "Cache-Control: no-store\r\n"
                  "Transfer-Encoding: chunked\r\n"
                  "\r\n")
        <= 0) {
        if (tail_slot_acquired) {
            cs_logs_tail_release();
        }
        close(fd);
        return 1;
    }

    if (cs_logs_send_recent_lines(conn, fd) != 0) {
        if (tail_slot_acquired) {
            cs_logs_tail_release();
        }
        close(fd);
        return 1;
    }
    offset = lseek(fd, 0, SEEK_END);
    started_at = time(NULL);
    last_sent_at = started_at;

    for (;;) {
        ssize_t nread = read(fd, buffer, sizeof(buffer));
        time_t now = time(NULL);

        if (nread > 0) {
            offset += nread;
            if (mg_send_chunk(conn, buffer, (unsigned int) nread) < 0) {
                break;
            }
            last_sent_at = now;
            continue;
        }
        if (nread < 0) {
            break;
        }
        if (now - started_at >= CS_LOGS_TAIL_MAX_STREAM_SECONDS
            || now - last_sent_at >= CS_LOGS_TAIL_IDLE_SECONDS) {
            break;
        }

        {
            struct stat st;

            if (fstat(fd, &st) != 0) {
                break;
            }
            if (st.st_size < offset) {
                offset = lseek(fd, 0, SEEK_SET);
            }
        }

        usleep(100000);
    }

    (void) mg_send_chunk(conn, "", 0);
    if (tail_slot_acquired) {
        cs_logs_tail_release();
    }
    close(fd);
    return 1;
}
