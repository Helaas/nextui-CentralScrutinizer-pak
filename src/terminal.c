#include "cs_terminal.h"

#include "../third_party/jsmn/jsmn.h"
#include "cs_app.h"
#include "cs_auth.h"
#include "cs_server.h"
#include "cs_util.h"

#include "civetweb.h"

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct cs_terminal_session {
    struct cs_terminal_session *next;
    char ticket[65];
    char session_token[65];
    char origin[256];
    time_t expires_at;
    int authenticated;
    int closing;
    int pty_fd;
    pid_t child_pid;
    pthread_t reader_thread;
    int reader_started;
    struct mg_connection *conn;
    pthread_mutex_t mutex;
} cs_terminal_session;

struct cs_terminal_manager {
    pthread_mutex_t mutex;
    cs_terminal_session *sessions;
    cs_app *app;
};

#define CS_TERMINAL_TICKET_TTL_SECONDS 10

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

static cs_terminal_manager *cs_terminal_manager_get(const cs_app *app) {
    return app ? app->terminal_manager : NULL;
}

static int cs_terminal_random_bytes(void *buffer, size_t len) {
    int fd;
    ssize_t nread;

    if (!buffer || len == 0) {
        return -1;
    }

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(buffer, len);
    return 0;
#endif

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    nread = read(fd, buffer, len);
    close(fd);
    return nread == (ssize_t) len ? 0 : -1;
}

static int cs_terminal_make_ticket(char *buffer, size_t buffer_len) {
    uint8_t bytes[16];
    size_t i;

    if (!buffer || buffer_len < 33) {
        return -1;
    }
    if (cs_terminal_random_bytes(bytes, sizeof(bytes)) != 0) {
        return -1;
    }

    for (i = 0; i < sizeof(bytes); ++i) {
        if (CS_SAFE_SNPRINTF(buffer + (i * 2), buffer_len - (i * 2), "%02x", bytes[i]) != 0) {
            return -1;
        }
    }
    buffer[sizeof(bytes) * 2] = '\0';
    return 0;
}

int cs_terminal_feature_enabled(const cs_app *app) {
    const char *override = getenv("CS_DISABLE_TERMINAL");

    if (!app) {
        return 0;
    }
    if (override && strcmp(override, "1") == 0) {
        return 0;
    }

    return cs_app_get_terminal_enabled(app);
}

static void cs_terminal_session_destroy(cs_terminal_session *session) {
    if (!session) {
        return;
    }

    if (session->pty_fd >= 0) {
        close(session->pty_fd);
        session->pty_fd = -1;
    }
    if (session->reader_started && !pthread_equal(pthread_self(), session->reader_thread)) {
        (void) pthread_join(session->reader_thread, NULL);
        session->reader_started = 0;
    }
    if (session->child_pid > 0) {
        (void) kill(session->child_pid, SIGHUP);
        (void) waitpid(session->child_pid, NULL, 0);
        session->child_pid = 0;
    }
    pthread_mutex_destroy(&session->mutex);
    free(session);
}

static void cs_terminal_remove_session_locked(cs_terminal_manager *manager, cs_terminal_session *session) {
    cs_terminal_session **cursor;

    if (!manager || !session) {
        return;
    }

    cursor = &manager->sessions;
    while (*cursor) {
        if (*cursor == session) {
            *cursor = session->next;
            session->next = NULL;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void cs_terminal_prune_locked(cs_terminal_manager *manager) {
    cs_terminal_session *current;
    cs_terminal_session *previous = NULL;
    time_t now = time(NULL);

    if (!manager) {
        return;
    }

    current = manager->sessions;
    while (current) {
        cs_terminal_session *next = current->next;

        if (!current->authenticated && current->expires_at <= now) {
            if (previous) {
                previous->next = next;
            } else {
                manager->sessions = next;
            }
            current->next = NULL;
            cs_terminal_session_destroy(current);
        } else {
            previous = current;
        }

        current = next;
    }
}

static int cs_terminal_send_text_locked(struct mg_connection *conn, const char *text) {
    int written;

    if (!conn || !text) {
        return -1;
    }

    mg_lock_connection(conn);
    written = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, text, strlen(text));
    mg_unlock_connection(conn);
    return written > 0 ? 0 : -1;
}

static int cs_terminal_send_binary_locked(struct mg_connection *conn, const void *data, size_t len) {
    int written;

    if (!conn || (!data && len > 0)) {
        return -1;
    }

    mg_lock_connection(conn);
    written = mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY, data, len);
    mg_unlock_connection(conn);
    return written > 0 ? 0 : -1;
}

static void cs_terminal_close_socket(struct mg_connection *conn, const char *reason) {
    if (!conn) {
        return;
    }

    if (reason) {
        (void) cs_terminal_send_text_locked(conn, reason);
    }

    mg_lock_connection(conn);
    (void) mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, "", 0);
    mg_unlock_connection(conn);
}

static int cs_terminal_resize_session(cs_terminal_session *session, int cols, int rows) {
    struct winsize size = {0};

    if (!session || session->pty_fd < 0 || cols < 1 || rows < 1) {
        return -1;
    }

    size.ws_col = (unsigned short) cols;
    size.ws_row = (unsigned short) rows;
    return ioctl(session->pty_fd, TIOCSWINSZ, &size);
}

static void *cs_terminal_reader_main(void *userdata) {
    cs_terminal_session *session = (cs_terminal_session *) userdata;
    char buffer[4096];

    if (!session) {
        return NULL;
    }

    for (;;) {
        ssize_t nread;
        struct mg_connection *conn = NULL;
        int should_close = 0;

        nread = read(session->pty_fd, buffer, sizeof(buffer));
        if (nread <= 0) {
            break;
        }

        pthread_mutex_lock(&session->mutex);
        if (!session->closing) {
            conn = session->conn;
        } else {
            should_close = 1;
        }
        pthread_mutex_unlock(&session->mutex);

        if (should_close) {
            break;
        }
        if (!conn) {
            continue;
        }
        if (cs_terminal_send_binary_locked(conn, buffer, (size_t) nread) != 0) {
            break;
        }
    }

    pthread_mutex_lock(&session->mutex);
    if (!session->closing) {
        struct mg_connection *conn = session->conn;

        session->closing = 1;
        pthread_mutex_unlock(&session->mutex);
        cs_terminal_close_socket(conn, "{\"type\":\"closed\"}");
    } else {
        pthread_mutex_unlock(&session->mutex);
    }

    return NULL;
}

static int cs_terminal_spawn_session(cs_terminal_session *session, const cs_app *app) {
    struct winsize size = {
        .ws_row = 24,
        .ws_col = 80,
    };
    const char *shell = getenv("CS_TERMINAL_SHELL");
    pid_t pid;

    if (!session || !app) {
        return -1;
    }
    if (!shell || shell[0] == '\0') {
        shell = "/bin/sh";
    }

    session->pty_fd = -1;
    pid = forkpty(&session->pty_fd, NULL, NULL, &size);
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        (void) chdir(app->paths.sdcard_root);
        (void) setenv("TERM", "xterm-256color", 1);
        if (strchr(shell, '/')) {
            execl(shell, shell, (char *) NULL);
        } else {
            execlp(shell, shell, (char *) NULL);
        }
        _exit(127);
    }

    session->child_pid = pid;
    if (pthread_create(&session->reader_thread, NULL, cs_terminal_reader_main, session) != 0) {
        close(session->pty_fd);
        session->pty_fd = -1;
        (void) kill(pid, SIGHUP);
        (void) waitpid(pid, NULL, 0);
        session->child_pid = 0;
        return -1;
    }

    session->reader_started = 1;
    return 0;
}

static int cs_terminal_origin_allowed(const struct mg_connection *conn) {
    const char *origin;
    const char *host;
    char expected_http[256];
    char expected_https[256];

    if (!conn) {
        return 0;
    }

    origin = mg_get_header(conn, "Origin");
    if (!origin || origin[0] == '\0') {
        return 0;
    }

    host = mg_get_header(conn, "Host");
    if (!host || host[0] == '\0') {
        return 0;
    }

    if (snprintf(expected_http, sizeof(expected_http), "http://%s", host) >= (int) sizeof(expected_http)
        || snprintf(expected_https, sizeof(expected_https), "https://%s", host) >= (int) sizeof(expected_https)) {
        return 0;
    }

    return strcmp(origin, expected_http) == 0 || strcmp(origin, expected_https) == 0;
}

static int cs_terminal_capture_request_origin(const struct mg_connection *conn, char *buffer, size_t buffer_len) {
    const char *origin;
    const char *host;
    const char *forwarded_proto;
    const char *scheme = "http";

    if (!conn || !buffer || buffer_len == 0) {
        return -1;
    }

    origin = mg_get_header(conn, "Origin");
    if (origin && origin[0] != '\0') {
        return snprintf(buffer, buffer_len, "%s", origin) < (int) buffer_len ? 0 : -1;
    }

    host = mg_get_header(conn, "Host");
    if (!host || host[0] == '\0') {
        return -1;
    }

    forwarded_proto = mg_get_header(conn, "X-Forwarded-Proto");
    if (forwarded_proto && strcmp(forwarded_proto, "https") == 0) {
        scheme = "https";
    }

    return snprintf(buffer, buffer_len, "%s://%s", scheme, host) < (int) buffer_len ? 0 : -1;
}

static int cs_terminal_copy_token(char *buffer, size_t buffer_len, const char *json, const jsmntok_t *token) {
    size_t token_len;

    if (!buffer || buffer_len == 0 || !json || !token || token->type != JSMN_STRING) {
        return -1;
    }

    token_len = (size_t) (token->end - token->start);
    if (token_len >= buffer_len) {
        return -1;
    }

    memcpy(buffer, json + token->start, token_len);
    buffer[token_len] = '\0';
    return 0;
}

static int cs_terminal_copy_int(int *out, const char *json, const jsmntok_t *token) {
    char number[32];
    size_t token_len;
    char *end = NULL;
    long value;

    if (!out || !json || !token || token->type != JSMN_PRIMITIVE) {
        return -1;
    }

    token_len = (size_t) (token->end - token->start);
    if (token_len == 0 || token_len >= sizeof(number)) {
        return -1;
    }

    memcpy(number, json + token->start, token_len);
    number[token_len] = '\0';
    errno = 0;
    value = strtol(number, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }

    *out = (int) value;
    return 0;
}

static int cs_terminal_parse_auth_message(const char *data, size_t len, char *ticket, size_t ticket_len) {
    char key[32];
    jsmn_parser parser;
    jsmntok_t tokens[16];
    int token_count;
    int i;
    int seen_type = 0;
    int seen_ticket = 0;

    if (!data || len == 0 || !ticket || ticket_len == 0) {
        return -1;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, data, len, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 1 || tokens[0].type != JSMN_OBJECT) {
        return -1;
    }

    for (i = 1; i + 1 < token_count; i += 2) {
        if (tokens[i].type != JSMN_STRING) {
            return -1;
        }
        if (cs_terminal_copy_token(key, sizeof(key), data, &tokens[i]) != 0) {
            return -1;
        }
        if (strcmp(key, "type") == 0) {
            if (cs_terminal_copy_token(key, sizeof(key), data, &tokens[i + 1]) != 0 || strcmp(key, "auth") != 0) {
                return -1;
            }
            seen_type = 1;
        } else if (strcmp(key, "ticket") == 0) {
            if (cs_terminal_copy_token(ticket, ticket_len, data, &tokens[i + 1]) != 0) {
                return -1;
            }
            seen_ticket = 1;
        }
    }

    return seen_type && seen_ticket ? 0 : -1;
}

static int cs_terminal_parse_resize_message(const char *data, size_t len, int *cols, int *rows) {
    char key[32];
    jsmn_parser parser;
    jsmntok_t tokens[16];
    int token_count;
    int i;
    int seen_type = 0;
    int seen_cols = 0;
    int seen_rows = 0;

    if (!data || len == 0 || !cols || !rows) {
        return -1;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, data, len, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 1 || tokens[0].type != JSMN_OBJECT) {
        return -1;
    }

    for (i = 1; i + 1 < token_count; i += 2) {
        if (cs_terminal_copy_token(key, sizeof(key), data, &tokens[i]) != 0) {
            return -1;
        }
        if (strcmp(key, "type") == 0) {
            if (cs_terminal_copy_token(key, sizeof(key), data, &tokens[i + 1]) != 0 || strcmp(key, "resize") != 0) {
                return -1;
            }
            seen_type = 1;
        } else if (strcmp(key, "cols") == 0) {
            if (cs_terminal_copy_int(cols, data, &tokens[i + 1]) != 0) {
                return -1;
            }
            seen_cols = 1;
        } else if (strcmp(key, "rows") == 0) {
            if (cs_terminal_copy_int(rows, data, &tokens[i + 1]) != 0) {
                return -1;
            }
            seen_rows = 1;
        }
    }

    return seen_type && seen_cols && seen_rows ? 0 : -1;
}

static int cs_terminal_control_is_allowed(unsigned char ch) {
    return ch == '\t' || ch == '\n' || ch == '\r' || ch == '\b' || ch == 0x7f || ch == 0x03 || ch == 0x04
           || ch == 0x1a;
}

static int cs_terminal_escape_sequence_is_safe(const char *data, size_t len, size_t *sequence_len) {
    size_t i;

    if (!data || len < 3 || !sequence_len || data[0] != '\x1b') {
        return 0;
    }

    if (data[1] == '[') {
        for (i = 2; i < len; ++i) {
            unsigned char ch = (unsigned char) data[i];

            if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?' || ch == ':') {
                continue;
            }
            if (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' || ch == 'F' || ch == 'H' || ch == '~') {
                *sequence_len = i + 1;
                return 1;
            }
            return 0;
        }
    } else if (data[1] == 'O') {
        if (data[2] == 'A' || data[2] == 'B' || data[2] == 'C' || data[2] == 'D' || data[2] == 'F'
            || data[2] == 'H' || data[2] == 'P' || data[2] == 'Q' || data[2] == 'R' || data[2] == 'S') {
            *sequence_len = 3;
            return 1;
        }
    }

    return 0;
}

static int cs_terminal_write_input(cs_terminal_session *session, const char *data, size_t len) {
    char *filtered = NULL;
    size_t filtered_len = 0;
    size_t written_total = 0;
    size_t i;

    if (!session || !data || len == 0 || session->pty_fd < 0) {
        return -1;
    }

    filtered = (char *) malloc(len);
    if (!filtered) {
        return -1;
    }

    for (i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char) data[i];

        if (ch == '\x1b') {
            size_t sequence_len = 0;

            if (cs_terminal_escape_sequence_is_safe(data + i, len - i, &sequence_len)) {
                memcpy(filtered + filtered_len, data + i, sequence_len);
                filtered_len += sequence_len;
                i += sequence_len - 1;
            }
            continue;
        }
        if (ch >= 0x20 || cs_terminal_control_is_allowed(ch)) {
            filtered[filtered_len++] = (char) ch;
        }
    }

    while (written_total < filtered_len) {
        ssize_t written = write(session->pty_fd, filtered + written_total, filtered_len - written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(filtered);
            return -1;
        }
        written_total += (size_t) written;
    }

    free(filtered);
    return 0;
}

static cs_terminal_session *cs_terminal_find_ticket_locked(cs_terminal_manager *manager, const char *ticket) {
    cs_terminal_session *session;
    size_t ticket_len;

    if (!manager || !ticket) {
        return NULL;
    }

    ticket_len = strlen(ticket);
    for (session = manager->sessions; session; session = session->next) {
        if (!session->closing && !session->authenticated && ticket_len == strlen(session->ticket)
            && cs_const_time_memcmp(session->ticket, ticket, ticket_len) == 0) {
            return session;
        }
    }

    return NULL;
}

static cs_terminal_session *cs_terminal_allocate_ticket(cs_terminal_manager *manager,
                                                        const char *session_token,
                                                        const char *origin) {
    cs_terminal_session *session;
    size_t count = 0;
    cs_terminal_session *cursor;

    if (!manager || !session_token || session_token[0] == '\0' || !origin || origin[0] == '\0') {
        return NULL;
    }

    for (cursor = manager->sessions; cursor; cursor = cursor->next) {
        count += 1;
    }
    if (count >= 32) {
        return NULL;
    }

    session = (cs_terminal_session *) calloc(1, sizeof(*session));
    if (!session) {
        return NULL;
    }

    session->pty_fd = -1;
    if (pthread_mutex_init(&session->mutex, NULL) != 0 || cs_terminal_make_ticket(session->ticket, sizeof(session->ticket)) != 0) {
        pthread_mutex_destroy(&session->mutex);
        free(session);
        return NULL;
    }
    if (snprintf(session->session_token, sizeof(session->session_token), "%s", session_token)
            >= (int) sizeof(session->session_token)
        || snprintf(session->origin, sizeof(session->origin), "%s", origin) >= (int) sizeof(session->origin)) {
        pthread_mutex_destroy(&session->mutex);
        free(session);
        return NULL;
    }
    session->expires_at = time(NULL) + CS_TERMINAL_TICKET_TTL_SECONDS;
    session->next = manager->sessions;
    manager->sessions = session;
    return session;
}

static cs_terminal_session *cs_terminal_attach_ticket(cs_app *app, struct mg_connection *conn, const char *ticket) {
    cs_terminal_manager *manager = cs_terminal_manager_get(app);
    cs_terminal_session *session = NULL;
    const char *cookie = mg_get_header(conn, "Cookie");
    const char *origin = mg_get_header(conn, "Origin");
    char cookie_token[sizeof(((cs_terminal_session *) 0)->session_token)];

    if (!manager || !conn || !ticket) {
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);
    cs_terminal_prune_locked(manager);
    if (cs_terminal_feature_enabled(app)) {
        session = cs_terminal_find_ticket_locked(manager, ticket);
        if (session && session->expires_at > time(NULL)) {
            pthread_mutex_lock(&session->mutex);
            if (session->closing || session->authenticated || !origin || origin[0] == '\0'
                || cs_server_copy_cookie_token(cookie, cookie_token, sizeof(cookie_token)) != 0
                || strcmp(origin, session->origin) != 0
                || strlen(cookie_token) != strlen(session->session_token)
                || cs_const_time_memcmp(cookie_token, session->session_token, strlen(session->session_token)) != 0) {
                pthread_mutex_unlock(&session->mutex);
                session = NULL;
            } else {
                session->authenticated = 1;
                session->conn = conn;
                pthread_mutex_unlock(&session->mutex);
            }
        } else {
            session = NULL;
        }
    }
    pthread_mutex_unlock(&manager->mutex);

    if (!session) {
        return NULL;
    }

    if (cs_terminal_spawn_session(session, app) != 0) {
        pthread_mutex_lock(&manager->mutex);
        cs_terminal_remove_session_locked(manager, session);
        pthread_mutex_unlock(&manager->mutex);
        pthread_mutex_lock(&session->mutex);
        session->authenticated = 0;
        session->conn = NULL;
        pthread_mutex_unlock(&session->mutex);
        cs_terminal_session_destroy(session);
        return NULL;
    }

    mg_set_user_connection_data(conn, session);
    return session;
}

int cs_terminal_manager_init(cs_app *app) {
    cs_terminal_manager *manager;

    if (!app) {
        return -1;
    }

    manager = (cs_terminal_manager *) calloc(1, sizeof(*manager));
    if (!manager) {
        return -1;
    }
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        free(manager);
        return -1;
    }

    manager->app = app;
    app->terminal_manager = manager;
    return 0;
}

void cs_terminal_manager_shutdown(cs_app *app) {
    cs_terminal_manager *manager;
    cs_terminal_session *session;

    manager = cs_terminal_manager_get(app);
    if (!manager) {
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    session = manager->sessions;
    manager->sessions = NULL;
    pthread_mutex_unlock(&manager->mutex);

    while (session) {
        cs_terminal_session *next = session->next;

        session->next = NULL;
        session->closing = 1;
        cs_terminal_session_destroy(session);
        session = next;
    }

    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    app->terminal_manager = NULL;
}

void cs_terminal_manager_close_all(cs_app *app, const char *reason) {
    cs_terminal_manager *manager = cs_terminal_manager_get(app);
    cs_terminal_session *current;
    cs_terminal_session *pending = NULL;
    cs_terminal_session *pending_tail = NULL;
    cs_terminal_session *active[32];
    size_t active_count = 0;

    if (!manager) {
        return;
    }

    pthread_mutex_lock(&manager->mutex);
    current = manager->sessions;
    while (current) {
        cs_terminal_session *next = current->next;

        if (!current->authenticated || !current->conn) {
            cs_terminal_remove_session_locked(manager, current);
            current->next = NULL;
            if (pending_tail) {
                pending_tail->next = current;
            } else {
                pending = current;
            }
            pending_tail = current;
        } else if (active_count < sizeof(active) / sizeof(active[0])) {
            active[active_count++] = current;
        }

        current = next;
    }
    pthread_mutex_unlock(&manager->mutex);

    while (pending) {
        cs_terminal_session *next = pending->next;

        pending->next = NULL;
        cs_terminal_session_destroy(pending);
        pending = next;
    }

    while (active_count > 0) {
        cs_terminal_session *session = active[--active_count];
        struct mg_connection *conn;

        pthread_mutex_lock(&session->mutex);
        session->closing = 1;
        conn = session->conn;
        if (session->pty_fd >= 0) {
            close(session->pty_fd);
            session->pty_fd = -1;
        }
        if (session->child_pid > 0) {
            (void) kill(session->child_pid, SIGHUP);
        }
        pthread_mutex_unlock(&session->mutex);

        cs_terminal_close_socket(conn, reason);
    }
}

static int cs_terminal_ws_connect(const struct mg_connection *conn, void *cbdata) {
    const cs_app *app = (const cs_app *) cbdata;
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!app || !cs_terminal_feature_enabled(app)) {
        return 1;
    }
    if (!cs_server_cookie_is_valid(cookie)) {
        return 1;
    }
    if (!cs_terminal_origin_allowed(conn)) {
        return 1;
    }

    return 0;
}

static void cs_terminal_ws_ready(struct mg_connection *conn, void *cbdata) {
    (void) cbdata;
    (void) conn;
}

static int cs_terminal_ws_data(struct mg_connection *conn, int bits, char *data, size_t len, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_terminal_session *session = (cs_terminal_session *) mg_get_user_connection_data(conn);

    if (!app) {
        return 0;
    }
    if ((bits & 0x0f) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) {
        return 0;
    }
    if (session == NULL) {
        char ticket[80];

        if (!cs_terminal_feature_enabled(app)
            || (bits & 0x0f) != MG_WEBSOCKET_OPCODE_TEXT
            || cs_terminal_parse_auth_message(data, len, ticket, sizeof(ticket)) != 0
            || cs_terminal_attach_ticket(app, conn, ticket) == NULL) {
            cs_terminal_close_socket(conn, "{\"type\":\"error\",\"error\":\"terminal_disabled\"}");
            return 0;
        }

        (void) cs_terminal_send_text_locked(conn, "{\"type\":\"ready\"}");
        return 1;
    }

    if ((bits & 0x0f) == MG_WEBSOCKET_OPCODE_TEXT) {
        int cols = 0;
        int rows = 0;

        if (len > 0 && data[0] == '{' && cs_terminal_parse_resize_message(data, len, &cols, &rows) == 0) {
            pthread_mutex_lock(&session->mutex);
            (void) cs_terminal_resize_session(session, cols, rows);
            pthread_mutex_unlock(&session->mutex);
            return 1;
        }
    }

    pthread_mutex_lock(&session->mutex);
    if (!session->closing) {
        (void) cs_terminal_write_input(session, data, len);
    }
    pthread_mutex_unlock(&session->mutex);
    return 1;
}

static void cs_terminal_ws_close(const struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_terminal_manager *manager = cs_terminal_manager_get(app);
    cs_terminal_session *session = (cs_terminal_session *) mg_get_user_connection_data(conn);

    if (!manager || !session) {
        return;
    }

    pthread_mutex_lock(&session->mutex);
    session->conn = NULL;
    session->closing = 1;
    if (session->pty_fd >= 0) {
        close(session->pty_fd);
        session->pty_fd = -1;
    }
    if (session->child_pid > 0) {
        (void) kill(session->child_pid, SIGHUP);
    }
    pthread_mutex_unlock(&session->mutex);

    pthread_mutex_lock(&manager->mutex);
    cs_terminal_remove_session_locked(manager, session);
    pthread_mutex_unlock(&manager->mutex);

    cs_terminal_session_destroy(session);
}

int cs_terminal_route_session_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_terminal_manager *manager = cs_terminal_manager_get(app);
    const char *cookie = mg_get_header(conn, "Cookie");
    cs_terminal_session *session;
    char body[160];
    char origin[sizeof(((cs_terminal_session *) 0)->origin)];
    char session_token[sizeof(((cs_terminal_session *) 0)->session_token)];
    int written;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app || !manager) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }
    if (!cs_terminal_feature_enabled(app)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"error\":\"terminal_disabled\"}");
    }
    if (cs_server_copy_cookie_token(cookie, session_token, sizeof(session_token)) != 0
        || cs_terminal_capture_request_origin(conn, origin, sizeof(origin)) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_origin\"}");
    }

    pthread_mutex_lock(&manager->mutex);
    cs_terminal_prune_locked(manager);
    session = cs_terminal_allocate_ticket(manager, session_token, origin);
    pthread_mutex_unlock(&manager->mutex);
    if (!session) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"ticket_failed\"}");
    }

    written = snprintf(body,
                       sizeof(body),
                       "{\"ticket\":\"%s\",\"expiresIn\":%d}",
                       session->ticket,
                       CS_TERMINAL_TICKET_TTL_SECONDS);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"ticket_failed\"}");
    }

    return cs_write_json(conn, 200, "OK", body);
}

void cs_terminal_register_websocket(struct mg_context *ctx, struct cs_app *app) {
    if (!ctx || !app) {
        return;
    }

    mg_set_websocket_handler(ctx,
                             "/api/terminal/socket",
                             cs_terminal_ws_connect,
                             cs_terminal_ws_ready,
                             cs_terminal_ws_data,
                             cs_terminal_ws_close,
                             app);
}
