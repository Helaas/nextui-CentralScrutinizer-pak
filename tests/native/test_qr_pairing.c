#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_app.h"
#include "cs_paths.h"
#include "cs_session.h"
#include "cs_server.h"

#if defined(CS_TESTING)
void cs_server_force_qr_pair_token_expiry_for_test(void);
int cs_server_force_trust_last_seen_ago_for_test(const char *browser_id, long long seconds_ago);
#endif

static void extract_cookie_value(const char *response, char *buffer, size_t buffer_len) {
    const char *header;
    const char *value_start;
    const char *value_end;
    size_t value_len;

    assert(response != NULL);
    assert(buffer != NULL);
    assert(buffer_len > 0);

    header = strstr(response, "Set-Cookie: ");
    assert(header != NULL);
    value_start = header + strlen("Set-Cookie: ");
    value_end = strchr(value_start, ';');
    assert(value_end != NULL);
    value_len = (size_t) (value_end - value_start);
    assert(value_len < buffer_len);
    memcpy(buffer, value_start, value_len);
    buffer[value_len] = '\0';
}

static int reserve_local_port(void) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
    assert(getsockname(fd, (struct sockaddr *) &addr, &addr_len) == 0);
    close(fd);
    return (int) ntohs(addr.sin_port);
}

static int read_http_response(int port, const char *path, char *buffer, size_t buffer_len) {
    struct sockaddr_in addr;
    int fd;
    size_t total = 0;
    char request[512];
    int written;

    assert(buffer != NULL);
    assert(buffer_len > 0);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);

    written = snprintf(request,
                       sizeof(request),
                       "GET %s HTTP/1.1\r\n"
                       "Host: 127.0.0.1:%d\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       path,
                       port);
    assert(written > 0 && (size_t) written < sizeof(request));
    assert(send(fd, request, (size_t) written, 0) == written);

    while (total + 1 < buffer_len) {
        ssize_t nread = recv(fd, buffer + total, buffer_len - total - 1, 0);

        if (nread < 0) {
            close(fd);
            return -1;
        }
        if (nread == 0) {
            break;
        }
        total += (size_t) nread;
    }

    buffer[total] = '\0';
    close(fd);
    return 0;
}

static int post_form_response(int port, const char *path, const char *body, char *buffer, size_t buffer_len) {
    struct sockaddr_in addr;
    int fd;
    size_t total = 0;
    char request[1024];
    int written;
    size_t body_len;

    assert(path != NULL);
    assert(body != NULL);
    assert(buffer != NULL);
    assert(buffer_len > 0);

    body_len = strlen(body);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);

    written = snprintf(request,
                       sizeof(request),
                       "POST %s HTTP/1.1\r\n"
                       "Host: 127.0.0.1:%d\r\n"
                       "Connection: close\r\n"
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: %zu\r\n"
                       "\r\n"
                       "%s",
                       path,
                       port,
                       body_len,
                       body);
    assert(written > 0 && (size_t) written < sizeof(request));
    assert(send(fd, request, (size_t) written, 0) == written);

    while (total + 1 < buffer_len) {
        ssize_t nread = recv(fd, buffer + total, buffer_len - total - 1, 0);

        if (nread < 0) {
            close(fd);
            return -1;
        }
        if (nread == 0) {
            break;
        }
        total += (size_t) nread;
    }

    buffer[total] = '\0';
    close(fd);
    return 0;
}

int main(void) {
    char template[] = "/tmp/cs-qr-test-XXXXXX";
    char web_root[CS_PATH_MAX];
    char pairing_code[8];
    char token[64];
    char cookie_header[128];
    char path[256];
    char body[256];
    char response[4096];
    char *temp_root;
    cs_app app;

    temp_root = mkdtemp(template);
    assert(temp_root != NULL);
    assert(snprintf(web_root, sizeof(web_root), "%s/web", temp_root) < (int) sizeof(web_root));
    assert(mkdir(web_root, 0775) == 0);

    assert(setenv("SDCARD_PATH", temp_root, 1) == 0);
    assert(setenv("CS_WEB_ROOT", web_root, 1) == 0);
    assert(setenv("CS_PAIRING_CODE", "7391", 1) == 0);

    memset(&app, 0, sizeof(app));
    atomic_init(&app.terminal_enabled, 0);
    assert(cs_paths_init(&app.paths) == 0);
    app.port = reserve_local_port();

    assert(cs_server_start(&app) == 0);
    assert(cs_server_copy_pairing_code(pairing_code, sizeof(pairing_code)) == 0);
    assert(strcmp(pairing_code, "7391") == 0);

    assert(snprintf(body, sizeof(body), "browser_id=desktop-code-1&code=7391") < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"trustedCount\":1") != NULL);

    assert(snprintf(body, sizeof(body), "browser_id=desktop-code-2&code=7391") < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 403 Forbidden") != NULL);
    assert(strstr(response, "\"error\":\"invalid_code\"") != NULL);

    assert(cs_server_reset_session() == 0);
    assert(cs_server_copy_pairing_code(pairing_code, sizeof(pairing_code)) == 0);
    assert(strcmp(pairing_code, "7391") == 0);

    cs_server_stop();
    assert(setenv("CS_PAIRING_CODE_REUSE", "1", 1) == 0);
    assert(cs_server_start(&app) == 0);
    assert(cs_server_copy_pairing_code(pairing_code, sizeof(pairing_code)) == 0);
    assert(strcmp(pairing_code, "7391") == 0);

    assert(snprintf(body, sizeof(body), "browser_id=desktop-code-reuse-1&code=7391") < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"trustedCount\":1") != NULL);
    assert(cs_server_copy_pairing_code(pairing_code, sizeof(pairing_code)) == 0);
    assert(strcmp(pairing_code, "7391") == 0);

    assert(snprintf(body, sizeof(body), "browser_id=desktop-code-reuse-2&code=7391") < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"trustedCount\":2") != NULL);
    extract_cookie_value(response, cookie_header, sizeof(cookie_header));
    assert(cs_server_cookie_is_valid(cookie_header) == 1);
    assert(cs_server_get_trusted_count() == 2);

    assert(cs_server_reset_session() == 0);
    assert(unsetenv("CS_PAIRING_CODE_REUSE") == 0);

    assert(cs_server_issue_qr_pair_token(token, sizeof(token)) == 0);
    assert(token[0] != '\0');
    assert(snprintf(path, sizeof(path), "%s?token=%s", CS_SERVER_QR_PAIR_PATH, token) < (int) sizeof(path));
    assert(read_http_response(app.port, path, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 303 See Other") != NULL);
    assert(strstr(response, "Location: /?pairQr=") != NULL);
    assert(strstr(response, token) != NULL);
    assert(cs_server_get_paired() == 0);

    assert(snprintf(body, sizeof(body), "browser_id=desktop-1&qr_token=%s", token) < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "Set-Cookie: cs_trust=") != NULL);
    assert(strstr(response, "SameSite=Strict") != NULL);
    assert(strstr(response, "X-Content-Type-Options: nosniff\r\n") != NULL);
    assert(strstr(response, "\"trustedCount\":1") != NULL);
    extract_cookie_value(response, cookie_header, sizeof(cookie_header));
    assert(cs_server_cookie_is_valid(cookie_header) == 1);
    assert(cs_server_get_paired() == 1);

#if defined(CS_TESTING)
    assert(cs_server_force_trust_last_seen_ago_for_test("desktop-1", CS_SESSION_IDLE_TIMEOUT_SECONDS + 1) == 0);
    assert(cs_server_cookie_is_valid(cookie_header) == 0);
    assert(cs_server_get_trusted_count() == 0);
#endif

    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 403 Forbidden") != NULL);
    assert(strstr(response, "\"error\":\"invalid_qr_token\"") != NULL);

    assert(cs_server_reset_session() == 0);
    assert(cs_server_issue_qr_pair_token(token, sizeof(token)) == 0);
#if defined(CS_TESTING)
    cs_server_force_qr_pair_token_expiry_for_test();
#endif
    assert(snprintf(body, sizeof(body), "browser_id=desktop-1&qr_token=%s", token) < (int) sizeof(body));
    assert(post_form_response(app.port, "/api/pair", body, response, sizeof(response)) == 0);
    assert(strstr(response, "HTTP/1.1 410 Gone") != NULL);
    assert(strstr(response, "\"error\":\"qr_expired\"") != NULL);

    cs_server_stop();
    unsetenv("CS_PAIRING_CODE");
    unsetenv("CS_PAIRING_CODE_REUSE");
    rmdir(web_root);
    rmdir(temp_root);
    return 0;
}
