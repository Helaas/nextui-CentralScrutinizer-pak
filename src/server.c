#include "cs_app.h"
#include "cs_auth.h"
#include "cs_session.h"
#include "cs_server.h"
#include "cs_terminal.h"
#include "cs_util.h"

#include "civetweb.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CS_PAIRING_MAX_FAILURES 5
#define CS_PAIRING_LOCKOUT_SECONDS 30
#define CS_QR_PAIR_TOKEN_BYTES 16
#define CS_QR_PAIR_TOKEN_TTL_SECONDS 120
#define CS_TRUST_TOKEN_BYTES 16
#define CS_TRUST_TOKEN_HEX_LEN (CS_TRUST_TOKEN_BYTES * 2)
#define CS_TRUST_STORE_PRUNE_INTERVAL_SECONDS 60
#define CS_TRUST_STORE_TOUCH_INTERVAL_SECONDS 60
#define CS_TRUST_STORE_FLUSH_INTERVAL_SECONDS 60

static struct mg_context *g_context = NULL;
static pthread_mutex_t g_server_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_pairing_code[sizeof(((cs_pairing *) 0)->code)] = {0};
static char g_qr_pair_token[(CS_QR_PAIR_TOKEN_BYTES * 2) + 1] = {0};
static time_t g_qr_pair_token_expires_at = 0;
static unsigned int g_pairing_failures = 0;
static time_t g_pairing_throttle_until = 0;
static unsigned int g_qr_pair_failures = 0;
static time_t g_qr_pair_throttle_until = 0;
static cs_trust_store g_trust_store = {0};
static char g_trust_store_path[CS_PATH_MAX] = {0};
static int g_trust_store_dirty = 0;
static time_t g_last_trust_store_prune_at = 0;
static time_t g_last_trust_store_flush_attempt_at = 0;

int cs_route_status_handler(struct mg_connection *conn, void *cbdata);
int cs_route_session_handler(struct mg_connection *conn, void *cbdata);
int cs_route_pair_handler(struct mg_connection *conn, void *cbdata);
int cs_route_pair_qr_handler(struct mg_connection *conn, void *cbdata);
int cs_route_revoke_handler(struct mg_connection *conn, void *cbdata);
int cs_route_platforms_handler(struct mg_connection *conn, void *cbdata);
int cs_route_browser_handler(struct mg_connection *conn, void *cbdata);
int cs_route_states_handler(struct mg_connection *conn, void *cbdata);
int cs_route_logs_handler(struct mg_connection *conn, void *cbdata);
int cs_route_logs_download_handler(struct mg_connection *conn, void *cbdata);
int cs_route_logs_tail_handler(struct mg_connection *conn, void *cbdata);
int cs_route_upload_handler(struct mg_connection *conn, void *cbdata);
int cs_route_rename_handler(struct mg_connection *conn, void *cbdata);
int cs_route_delete_handler(struct mg_connection *conn, void *cbdata);
int cs_route_create_folder_handler(struct mg_connection *conn, void *cbdata);
int cs_route_download_handler(struct mg_connection *conn, void *cbdata);
int cs_route_replace_art_handler(struct mg_connection *conn, void *cbdata);
int cs_route_write_handler(struct mg_connection *conn, void *cbdata);
int cs_route_file_search_handler(struct mg_connection *conn, void *cbdata);
int cs_route_mac_dotfiles_handler(struct mg_connection *conn, void *cbdata);
static int cs_server_randomize_pairing_code_locked(void);
static int cs_server_reset_pairing_code_locked(void);
static int cs_server_advance_pairing_code_locked(void);
static int cs_server_save_trust_store_locked(const cs_trust_store *store);
static int cs_server_commit_trust_store_locked(const cs_trust_store *store, time_t now);
static int cs_server_flush_trust_store_locked(time_t now, int force);
static int cs_server_count_active_trust_locked(time_t now);
static int cs_server_remove_inactive_trust_locked(time_t now);
static int cs_server_prune_expired_trust_locked(time_t now);

const char *cs_server_security_headers(void) {
    return CS_SERVER_SECURITY_HEADERS_HTTP;
}

static int cs_server_write_api_not_found(struct mg_connection *conn) {
    static const char body[] = "{\"error\":\"not_found\"}";

    mg_printf(conn,
              "HTTP/1.1 404 Not Found\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              sizeof(body) - 1,
              body);
    return 1;
}

static int cs_server_begin_request(struct mg_connection *conn) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    const char *uri;
    cs_app *app;

    if (!request) {
        return 0;
    }

    uri = request->local_uri ? request->local_uri : request->request_uri;
    app = (cs_app *) request->user_data;
    if (!uri || !app) {
        return 0;
    }

    if (strcmp(uri, "/api/status") == 0) {
        return cs_route_status_handler(conn, app);
    }
    if (strcmp(uri, "/api/session") == 0) {
        return cs_route_session_handler(conn, app);
    }
    if (strcmp(uri, "/api/pair") == 0) {
        return cs_route_pair_handler(conn, app);
    }
    if (strcmp(uri, CS_SERVER_QR_PAIR_PATH) == 0) {
        return cs_route_pair_qr_handler(conn, app);
    }
    if (strcmp(uri, "/api/revoke") == 0) {
        return cs_route_revoke_handler(conn, app);
    }
    if (strcmp(uri, "/api/platforms") == 0) {
        return cs_route_platforms_handler(conn, app);
    }
    if (strcmp(uri, "/api/browser") == 0) {
        return cs_route_browser_handler(conn, app);
    }
    if (strcmp(uri, "/api/states") == 0) {
        return cs_route_states_handler(conn, app);
    }
    if (strcmp(uri, "/api/logs") == 0) {
        return cs_route_logs_handler(conn, app);
    }
    if (strcmp(uri, "/api/logs/download") == 0) {
        return cs_route_logs_download_handler(conn, app);
    }
    if (strcmp(uri, "/api/logs/tail") == 0) {
        return cs_route_logs_tail_handler(conn, app);
    }
    if (strcmp(uri, "/api/upload") == 0) {
        return cs_route_upload_handler(conn, app);
    }
    if (strcmp(uri, "/api/item/rename") == 0) {
        return cs_route_rename_handler(conn, app);
    }
    if (strcmp(uri, "/api/item/delete") == 0) {
        return cs_route_delete_handler(conn, app);
    }
    if (strcmp(uri, "/api/folder/create") == 0) {
        return cs_route_create_folder_handler(conn, app);
    }
    if (strcmp(uri, "/api/download") == 0) {
        return cs_route_download_handler(conn, app);
    }
    if (strcmp(uri, "/api/files/search") == 0) {
        return cs_route_file_search_handler(conn, app);
    }
    if (strcmp(uri, "/api/art/replace") == 0) {
        return cs_route_replace_art_handler(conn, app);
    }
    if (strcmp(uri, "/api/item/write") == 0) {
        return cs_route_write_handler(conn, app);
    }
    if (strcmp(uri, "/api/tools/mac-dotfiles") == 0) {
        return cs_route_mac_dotfiles_handler(conn, app);
    }
    if (strcmp(uri, "/api/terminal/session") == 0 || strcmp(uri, "/api/terminal/socket") == 0) {
        return strcmp(uri, "/api/terminal/session") == 0 ? cs_terminal_route_session_handler(conn, app) : 0;
    }
    if (strncmp(uri, "/api/", 5) == 0) {
        return cs_server_write_api_not_found(conn);
    }

    return 0;
}

static const struct mg_callbacks g_server_callbacks = {
    .begin_request = cs_server_begin_request,
};

static int cs_pairing_code_is_valid(const char *code) {
    int i;

    if (!code) {
        return 0;
    }

    for (i = 0; i < 4; ++i) {
        if (!isdigit((unsigned char) code[i])) {
            return 0;
        }
    }

    return code[4] == '\0';
}

static int cs_server_random_bytes(void *buffer, size_t len) {
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

static void cs_server_clear_qr_pair_token_locked(void) {
    memset(g_qr_pair_token, 0, sizeof(g_qr_pair_token));
    g_qr_pair_token_expires_at = 0;
}

static int cs_server_make_hex_token(char *buffer, size_t buffer_len, size_t byte_count) {
    uint8_t bytes[CS_QR_PAIR_TOKEN_BYTES];
    size_t i;

    if (!buffer || buffer_len == 0 || byte_count == 0 || byte_count > sizeof(bytes) || buffer_len < (byte_count * 2) + 1) {
        return -1;
    }
    if (cs_server_random_bytes(bytes, byte_count) != 0) {
        return -1;
    }

    for (i = 0; i < byte_count; ++i) {
        if (CS_SAFE_SNPRINTF(buffer + (i * 2), buffer_len - (i * 2), "%02x", bytes[i]) != 0) {
            return -1;
        }
    }
    buffer[byte_count * 2] = '\0';
    return 0;
}

static void cs_server_reset_pairing_attempts_locked(void) {
    g_pairing_failures = 0;
    g_pairing_throttle_until = 0;
}

static void cs_server_reset_qr_pairing_attempts_locked(void) {
    g_qr_pair_failures = 0;
    g_qr_pair_throttle_until = 0;
}

static int cs_server_generate_pairing_code_locked(void) {
    cs_pairing pairing;

    if (cs_pairing_generate(&pairing) != 0) {
        return -1;
    }

    memcpy(g_pairing_code, pairing.code, sizeof(g_pairing_code));
    return 0;
}

static int cs_server_get_fixed_pairing_code_locked(const char **code_out, int *reuse_out) {
    const char *override = getenv("CS_PAIRING_CODE");
    const char *reuse = getenv("CS_PAIRING_CODE_REUSE");

    if (code_out) {
        *code_out = NULL;
    }
    if (reuse_out) {
        *reuse_out = 0;
    }
    if (!cs_pairing_code_is_valid(override)) {
        return 0;
    }

    if (code_out) {
        *code_out = override;
    }
    if (reuse_out && reuse && strcmp(reuse, "1") == 0) {
        *reuse_out = 1;
    }
    return 1;
}

static int cs_server_set_pairing_code_locked(const char *code) {
    if (!cs_pairing_code_is_valid(code)) {
        return -1;
    }

    memcpy(g_pairing_code, code, sizeof(g_pairing_code));
    cs_server_reset_pairing_attempts_locked();
    return 0;
}

static int cs_server_ensure_parent_dir(const char *path) {
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

static int cs_server_save_trust_store_locked(const cs_trust_store *store) {
    if (!store || g_trust_store_path[0] == '\0') {
        return -1;
    }
    if (cs_server_ensure_parent_dir(g_trust_store_path) != 0) {
        return -1;
    }

    return cs_trust_store_save(g_trust_store_path, store);
}

static int cs_server_commit_trust_store_locked(const cs_trust_store *store, time_t now) {
    if (!store) {
        return -1;
    }
    if (cs_server_save_trust_store_locked(store) != 0) {
        return -1;
    }

    g_trust_store = *store;
    g_trust_store_dirty = 0;
    g_last_trust_store_flush_attempt_at = now;
    return 0;
}

static int cs_server_flush_trust_store_locked(time_t now, int force) {
    if (!g_trust_store_dirty) {
        return 0;
    }
    if (!force
        && g_last_trust_store_flush_attempt_at > 0
        && now >= g_last_trust_store_flush_attempt_at
        && now - g_last_trust_store_flush_attempt_at < CS_TRUST_STORE_FLUSH_INTERVAL_SECONDS) {
        return 0;
    }

    g_last_trust_store_flush_attempt_at = now;
    if (cs_server_save_trust_store_locked(&g_trust_store) != 0) {
        return -1;
    }

    g_trust_store_dirty = 0;
    return 0;
}

static int cs_server_trust_item_is_active_locked(const cs_trust_item *item, time_t now) {
    long long idle_cutoff = (long long) now - CS_SESSION_IDLE_TIMEOUT_SECONDS;

    if (!item) {
        return 0;
    }
    if (item->expires_at <= (long long) now) {
        return 0;
    }
    if (item->last_seen_at > 0 && item->last_seen_at <= idle_cutoff) {
        return 0;
    }

    return 1;
}

static int cs_server_count_active_trust_locked(time_t now) {
    int count = 0;
    size_t i;

    for (i = 0; i < g_trust_store.count; ++i) {
        if (cs_server_trust_item_is_active_locked(&g_trust_store.items[i], now)) {
            count += 1;
        }
    }

    return count;
}

static int cs_server_remove_inactive_trust_locked(time_t now) {
    size_t read_index;
    size_t write_index = 0;
    int removed = 0;

    for (read_index = 0; read_index < g_trust_store.count; ++read_index) {
        if (!cs_server_trust_item_is_active_locked(&g_trust_store.items[read_index], now)) {
            removed += 1;
            continue;
        }
        if (write_index != read_index) {
            g_trust_store.items[write_index] = g_trust_store.items[read_index];
        }
        write_index += 1;
    }

    while (write_index < g_trust_store.count) {
        memset(&g_trust_store.items[write_index], 0, sizeof(g_trust_store.items[write_index]));
        write_index += 1;
    }

    if (removed > 0) {
        g_trust_store.count -= (size_t) removed;
        g_trust_store_dirty = 1;
    }
    return removed;
}

static int cs_server_prune_expired_trust_locked(time_t now) {
    int removed;

    if (g_last_trust_store_prune_at > 0
        && now >= g_last_trust_store_prune_at
        && now - g_last_trust_store_prune_at < CS_TRUST_STORE_PRUNE_INTERVAL_SECONDS) {
        return 0;
    }
    g_last_trust_store_prune_at = now;

    removed = cs_server_remove_inactive_trust_locked(now);
    if (removed < 0) {
        return -1;
    }
    if (removed > 0) {
        (void) cs_server_flush_trust_store_locked(now, 0);
    }

    return removed;
}

static int cs_server_load_trust_store_locked(const cs_app *app) {
    cs_trust_store store = {0};
    time_t now;
    int removed;

    if (!app) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(g_trust_store_path,
                         sizeof(g_trust_store_path),
                         "%s/trusted-clients.json",
                         app->paths.shared_state_root)
        != 0) {
        return -1;
    }
    if (cs_trust_store_load(g_trust_store_path, &store) != 0) {
        return -1;
    }

    g_trust_store = store;
    g_trust_store_dirty = 0;
    g_last_trust_store_prune_at = 0;
    g_last_trust_store_flush_attempt_at = 0;
    now = time(NULL);
    removed = cs_server_remove_inactive_trust_locked(now);
    if (removed < 0) {
        return -1;
    }
    if (removed > 0) {
        (void) cs_server_flush_trust_store_locked(now, 0);
    }
    return 0;
}

static int cs_server_reset_session_locked(void) {
    cs_trust_store empty = {0};
    time_t now = time(NULL);

    if (cs_server_reset_pairing_code_locked() != 0) {
        return -1;
    }
    cs_server_clear_qr_pair_token_locked();
    cs_server_reset_qr_pairing_attempts_locked();
    if (g_trust_store_path[0] != '\0') {
        if (cs_server_commit_trust_store_locked(&empty, now) != 0) {
            return -1;
        }
    } else {
        g_trust_store = empty;
        g_trust_store_dirty = 0;
    }
    g_last_trust_store_prune_at = 0;
    return 0;
}

static int cs_server_revoke_token_locked(const char *trust_token) {
    cs_trust_store next_store;
    time_t now = time(NULL);
    int remove_status;

    if (!trust_token) {
        return -1;
    }

    (void) cs_server_prune_expired_trust_locked(now);
    next_store = g_trust_store;
    remove_status = cs_trust_store_remove_token(&next_store, trust_token);
    if (remove_status < 0) {
        return -1;
    }
    if (remove_status == 0) {
        return 0;
    }
    if (cs_server_commit_trust_store_locked(&next_store, now) != 0) {
        return -1;
    }

    g_last_trust_store_prune_at = 0;
    return 0;
}

static int cs_server_extract_cookie_value(const char *cookie_header,
                                          const char *name,
                                          char *buffer,
                                          size_t buffer_len) {
    size_t name_len;
    const char *cursor;

    if (!cookie_header || !name || !buffer || buffer_len == 0) {
        return -1;
    }

    name_len = strlen(name);
    cursor = cookie_header;
    while (*cursor != '\0') {
        const char *segment_end = strchr(cursor, ';');
        const char *equals = strchr(cursor, '=');
        size_t value_len;

        while (*cursor == ' ') {
            ++cursor;
        }

        if (!segment_end) {
            segment_end = cursor + strlen(cursor);
        }
        if (equals && equals < segment_end && (size_t) (equals - cursor) == name_len
            && strncmp(cursor, name, name_len) == 0) {
            value_len = (size_t) (segment_end - equals - 1);
            if (value_len >= buffer_len) {
                return -1;
            }
            memcpy(buffer, equals + 1, value_len);
            buffer[value_len] = '\0';
            return 0;
        }

        cursor = *segment_end == ';' ? segment_end + 1 : segment_end;
    }

    return -1;
}

static int cs_server_randomize_pairing_code_locked(void) {
    if (cs_server_generate_pairing_code_locked() != 0) {
        return -1;
    }

    cs_server_reset_pairing_attempts_locked();
    return 0;
}

static int cs_server_reset_pairing_code_locked(void) {
    const char *override = NULL;

    if (cs_server_get_fixed_pairing_code_locked(&override, NULL) > 0) {
        return cs_server_set_pairing_code_locked(override);
    }

    return cs_server_randomize_pairing_code_locked();
}

static int cs_server_advance_pairing_code_locked(void) {
    const char *override = NULL;
    int reuse = 0;

    if (cs_server_get_fixed_pairing_code_locked(&override, &reuse) > 0 && reuse) {
        return cs_server_set_pairing_code_locked(override);
    }

    return cs_server_randomize_pairing_code_locked();
}

int cs_server_get_paired(void) {
    return cs_server_get_trusted_count() > 0;
}

int cs_server_get_trusted_count(void) {
    int count;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_server_state_mutex);
    (void) cs_server_prune_expired_trust_locked(now);
    count = cs_server_count_active_trust_locked(now);
    pthread_mutex_unlock(&g_server_state_mutex);
    return count;
}

int cs_server_reset_session(void) {
    int rc = 0;

    pthread_mutex_lock(&g_server_state_mutex);
    if (cs_server_reset_session_locked() != 0) {
        rc = -1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return rc;
}

int cs_server_revoke_browser(const char *cookie_header) {
    char trust_token[CS_TRUST_TOKEN_HEX_LEN + 1];
    int rc = 0;

    if (cs_server_copy_cookie_token(cookie_header, trust_token, sizeof(trust_token)) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    if (cs_server_revoke_token_locked(trust_token) != 0) {
        rc = -1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return rc;
}

int cs_server_copy_pairing_code(char *buffer, size_t buffer_len) {
    int rc;

    if (!buffer || buffer_len == 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    rc = CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", g_pairing_code);
    pthread_mutex_unlock(&g_server_state_mutex);
    return rc;
}

int cs_server_copy_cookie_token(const char *cookie_header, char *buffer, size_t buffer_len) {
    return cs_server_extract_cookie_value(cookie_header, "cs_trust", buffer, buffer_len);
}

int cs_server_make_session_cookie(char *buffer, size_t buffer_len, const char *trust_token) {
    return cs_session_make_cookie(buffer, buffer_len, trust_token);
}

int cs_server_make_csrf_token(char *buffer, size_t buffer_len, const char *trust_token) {
    return cs_session_make_csrf(buffer, buffer_len, trust_token);
}

int cs_server_trust_browser(const char *browser_id, char *trust_token, size_t trust_token_len) {
    char token[CS_TRUST_TOKEN_HEX_LEN + 1];
    cs_trust_store next_store;
    time_t now = time(NULL);

    if (!browser_id || !trust_token || trust_token_len < sizeof(token)) {
        return -1;
    }
    if (cs_server_make_hex_token(token, sizeof(token), CS_TRUST_TOKEN_BYTES) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    (void) cs_server_prune_expired_trust_locked(now);

    next_store = g_trust_store;
    if (cs_trust_store_add(&next_store, browser_id, token) != 0
        || cs_server_commit_trust_store_locked(&next_store, now) != 0
        || CS_SAFE_SNPRINTF(trust_token, trust_token_len, "%s", token) != 0) {
        pthread_mutex_unlock(&g_server_state_mutex);
        return -1;
    }

    cs_server_clear_qr_pair_token_locked();
    cs_server_reset_qr_pairing_attempts_locked();
    pthread_mutex_unlock(&g_server_state_mutex);
    return 0;
}

int cs_server_issue_qr_pair_token(char *buffer, size_t buffer_len) {
    char token[sizeof(g_qr_pair_token)];
    int rc;

    if (!buffer || buffer_len == 0) {
        return -1;
    }
    if (cs_server_make_hex_token(token, sizeof(token), CS_QR_PAIR_TOKEN_BYTES) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    rc = CS_SAFE_SNPRINTF(g_qr_pair_token, sizeof(g_qr_pair_token), "%s", token);
    if (rc == 0) {
        g_qr_pair_token_expires_at = time(NULL) + CS_QR_PAIR_TOKEN_TTL_SECONDS;
        cs_server_reset_qr_pairing_attempts_locked();
        rc = CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", g_qr_pair_token);
        if (rc != 0) {
            cs_server_clear_qr_pair_token_locked();
        }
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return rc;
}

int cs_server_consume_qr_pair_token(const char *token, int *expired_out) {
    time_t now;
    size_t token_len;

    if (!token || !expired_out) {
        return -1;
    }

    *expired_out = 0;
    now = time(NULL);
    token_len = strlen(token);
    pthread_mutex_lock(&g_server_state_mutex);
    if (g_qr_pair_token[0] == '\0') {
        pthread_mutex_unlock(&g_server_state_mutex);
        return 0;
    }
    if (g_qr_pair_throttle_until > now) {
        pthread_mutex_unlock(&g_server_state_mutex);
        return 0;
    }
    if (g_qr_pair_token_expires_at <= now) {
        *expired_out = 1;
        cs_server_clear_qr_pair_token_locked();
        cs_server_reset_qr_pairing_attempts_locked();
        pthread_mutex_unlock(&g_server_state_mutex);
        return 0;
    }
    if (token_len != CS_QR_PAIR_TOKEN_BYTES * 2
        || cs_const_time_memcmp(token, g_qr_pair_token, CS_QR_PAIR_TOKEN_BYTES * 2) != 0) {
        g_qr_pair_failures += 1;
        if (g_qr_pair_failures >= CS_PAIRING_MAX_FAILURES) {
            g_qr_pair_throttle_until = now + CS_PAIRING_LOCKOUT_SECONDS;
        }
        pthread_mutex_unlock(&g_server_state_mutex);
        return 0;
    }

    cs_server_clear_qr_pair_token_locked();
    cs_server_reset_qr_pairing_attempts_locked();
    if (cs_server_randomize_pairing_code_locked() != 0) {
        pthread_mutex_unlock(&g_server_state_mutex);
        return -1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return 1;
}

int cs_server_qr_pair_token_is_active(const char *token) {
    time_t now;
    int is_active = 0;

    if (!token || token[0] == '\0') {
        return 0;
    }

    now = time(NULL);
    pthread_mutex_lock(&g_server_state_mutex);
    if (g_qr_pair_token_expires_at <= now) {
        cs_server_clear_qr_pair_token_locked();
        cs_server_reset_qr_pairing_attempts_locked();
    } else if (strcmp(g_qr_pair_token, token) == 0) {
        is_active = 1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return is_active;
}

int cs_server_get_qr_pair_token_ttl_seconds(void) {
    return CS_QR_PAIR_TOKEN_TTL_SECONDS;
}

int cs_server_consume_pairing_code(const char *code, int *throttled_out) {
    time_t now;
    int matched = 0;
    size_t code_len;

    if (!code || !throttled_out) {
        return -1;
    }

    *throttled_out = 0;
    now = time(NULL);
    code_len = strlen(code);
    pthread_mutex_lock(&g_server_state_mutex);
    if (g_pairing_throttle_until > now) {
        *throttled_out = 1;
        pthread_mutex_unlock(&g_server_state_mutex);
        return 0;
    }
    if (code_len == sizeof(g_pairing_code) - 1
        && cs_const_time_memcmp(code, g_pairing_code, sizeof(g_pairing_code) - 1) == 0) {
        matched = 1;
        if (cs_server_advance_pairing_code_locked() != 0) {
            pthread_mutex_unlock(&g_server_state_mutex);
            return -1;
        }
        pthread_mutex_unlock(&g_server_state_mutex);
        return 1;
    }

    g_pairing_failures += 1;
    if (g_pairing_failures >= CS_PAIRING_MAX_FAILURES) {
        g_pairing_throttle_until = now + CS_PAIRING_LOCKOUT_SECONDS;
        *throttled_out = 1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return matched;
}

int cs_server_make_clear_cookie(char *buffer, size_t buffer_len) {
    return CS_SAFE_SNPRINTF(buffer,
                            buffer_len,
                            "cs_trust=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

int cs_server_cookie_is_valid(const char *cookie_header) {
    char token[CS_TRUST_TOKEN_HEX_LEN + 1];
    int touch_status;
    time_t now;

    if (cs_server_extract_cookie_value(cookie_header, "cs_trust", token, sizeof(token)) != 0) {
        return 0;
    }

    now = time(NULL);
    pthread_mutex_lock(&g_server_state_mutex);
    (void) cs_server_prune_expired_trust_locked(now);
    touch_status = cs_trust_store_touch_token(&g_trust_store,
                                              token,
                                              (long long) now,
                                              CS_SESSION_IDLE_TIMEOUT_SECONDS,
                                              CS_TRUST_STORE_TOUCH_INTERVAL_SECONDS);
    if (touch_status == 1) {
        g_trust_store_dirty = 1;
        (void) cs_server_flush_trust_store_locked(now, 0);
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return touch_status > 0;
}

int cs_server_csrf_is_valid(const char *cookie_header, const char *csrf_header) {
    char token[CS_TRUST_TOKEN_HEX_LEN + 1];
    char csrf_token[CS_SESSION_CSRF_TOKEN_HEX_LEN + 1];
    size_t expected_len;
    size_t actual_len;

    if (!cookie_header || !csrf_header) {
        return 0;
    }
    if (cs_server_copy_cookie_token(cookie_header, token, sizeof(token)) != 0
        || cs_session_make_csrf(csrf_token, sizeof(csrf_token), token) != 0) {
        return 0;
    }

    expected_len = strlen(csrf_token);
    actual_len = strlen(csrf_header);
    return actual_len == expected_len && cs_const_time_memcmp(csrf_header, csrf_token, expected_len) == 0;
}

int cs_server_request_csrf_is_valid(const struct mg_connection *conn, int allow_query_param) {
    const char *cookie = mg_get_header(conn, "Cookie");
    const char *csrf = mg_get_header(conn, "X-CS-CSRF");

    if (cs_server_csrf_is_valid(cookie, csrf)) {
        return 1;
    }
    if (allow_query_param) {
        const struct mg_request_info *request = mg_get_request_info(conn);
        char query_token[CS_SESSION_CSRF_TOKEN_HEX_LEN + 1];

        if (request && request->query_string
            && mg_get_var(request->query_string, strlen(request->query_string), "csrf", query_token, sizeof(query_token))
                   > 0) {
            return cs_server_csrf_is_valid(cookie, query_token);
        }
    }

    return 0;
}

static int cs_server_bind_interface_priority(const char *name) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "wlan0") == 0) {
        return 3;
    }
    if (strcmp(name, "ap0") == 0 || strcmp(name, "eth0") == 0) {
        return 2;
    }

    return 1;
}

static int cs_server_copy_default_bind_addr(char *buffer, size_t buffer_len) {
    const char *override = getenv("CS_BIND_ADDR");

    if (!buffer || buffer_len == 0) {
        return -1;
    }
    if (override && override[0] != '\0') {
        return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", override);
    }

#if defined(PLATFORM_MAC)
    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", "127.0.0.1");
#else
    {
        struct ifaddrs *ifaddr = NULL;
        struct ifaddrs *ifa = NULL;
        char best_addr[64] = {0};
        int best_priority = 0;

        if (getifaddrs(&ifaddr) == 0) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                int priority;

                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
                    continue;
                }
                if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
                    continue;
                }

                priority = cs_server_bind_interface_priority(ifa->ifa_name);
                if (priority < best_priority) {
                    continue;
                }
                if (!inet_ntop(AF_INET,
                               &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr,
                               best_addr,
                               (socklen_t) sizeof(best_addr))) {
                    continue;
                }

                best_priority = priority;
                if (priority >= 3) {
                    break;
                }
            }
            freeifaddrs(ifaddr);
        }

        if (best_addr[0] == '\0') {
            return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", "127.0.0.1");
        }

        return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s", best_addr);
    }
#endif
}

int cs_server_start(struct cs_app *app) {
    char bind_addr[64];
    char listening_port[96];
    const char *options[] = {"document_root",
                             app ? app->paths.web_root : "",
                             "listening_ports",
                             listening_port,
                             "enable_directory_listing",
                             "no",
                             "static_file_max_age",
                             "0",
                             "additional_header",
                             cs_server_security_headers(),
                             NULL};

    if (!app || g_context != NULL) {
        return -1;
    }

    if (cs_session_init_csrf_secret() != 0
        || cs_server_copy_default_bind_addr(bind_addr, sizeof(bind_addr)) != 0
        || CS_SAFE_SNPRINTF(listening_port, sizeof(listening_port), "%s:%d", bind_addr, app->port) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    if (cs_server_load_trust_store_locked(app) != 0
        || cs_server_reset_pairing_code_locked() != 0) {
        pthread_mutex_unlock(&g_server_state_mutex);
        return -1;
    }
    cs_server_clear_qr_pair_token_locked();
    cs_server_reset_qr_pairing_attempts_locked();
    pthread_mutex_unlock(&g_server_state_mutex);

    g_context = mg_start(&g_server_callbacks, app, options);
    if (!g_context) {
        return -1;
    }
    cs_terminal_register_websocket(g_context, app);
    return 0;
}

void cs_server_stop(void) {
    if (g_context == NULL) {
        return;
    }

    mg_stop(g_context);
    g_context = NULL;

    pthread_mutex_lock(&g_server_state_mutex);
    (void) cs_server_flush_trust_store_locked(time(NULL), 1);
    cs_server_clear_qr_pair_token_locked();
    pthread_mutex_unlock(&g_server_state_mutex);
}

#if defined(CS_TESTING)
void cs_server_force_qr_pair_token_expiry_for_test(void) {
    pthread_mutex_lock(&g_server_state_mutex);
    if (g_qr_pair_token[0] != '\0') {
        g_qr_pair_token_expires_at = time(NULL) - 1;
    }
    pthread_mutex_unlock(&g_server_state_mutex);
}

int cs_server_force_trust_last_seen_ago_for_test(const char *browser_id, long long seconds_ago) {
    size_t i;
    time_t now = time(NULL);

    if (!browser_id || seconds_ago < 0) {
        return -1;
    }

    pthread_mutex_lock(&g_server_state_mutex);
    for (i = 0; i < g_trust_store.count; ++i) {
        if (strcmp(g_trust_store.items[i].browser_id, browser_id) == 0) {
            g_trust_store.items[i].last_seen_at = (long long) now - seconds_ago;
            g_trust_store_dirty = 1;
            g_last_trust_store_prune_at = 0;
            pthread_mutex_unlock(&g_server_state_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_server_state_mutex);
    return -1;
}
#endif
