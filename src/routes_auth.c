#include "cs_app.h"
#include "cs_session.h"
#include "cs_server.h"
#include "cs_terminal.h"
#include "civetweb.h"

#include <stdio.h>
#include <string.h>

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

static int cs_write_html(struct mg_connection *conn, int status, const char *reason, const char *title, const char *message) {
    char body[1024];
    int written;

    if (!conn) {
        return 0;
    }

    written = snprintf(body,
                       sizeof(body),
                       "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                       "content=\"width=device-width, initial-scale=1\"><title>%s</title></head><body "
                       "style=\"font-family:sans-serif;background:#101418;color:#f5f7fa;padding:2rem;line-height:1.5\">"
                       "<main style=\"max-width:32rem;margin:0 auto\"><h1 style=\"font-size:1.5rem\">%s</h1><p>%s</p></main>"
                       "</body></html>",
                       title ? title : "Central Scrutinizer",
                       title ? title : "Central Scrutinizer",
                       message ? message : "");
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return 0;
    }

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: text/html; charset=utf-8\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              status,
              reason,
              strlen(body),
              body);
    return 1;
}

static int cs_write_redirect(struct mg_connection *conn, const char *location) {
    if (!conn || !location) {
        return 0;
    }

    mg_printf(conn,
              "HTTP/1.1 303 See Other\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Location: %s\r\n"
              "Content-Length: 0\r\n"
              "\r\n",
              location);
    return 1;
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

int cs_route_pair_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[256];
    char browser_id[128];
    char code[16];
    char qr_token[64];
    char trust_token[64];
    char set_cookie[256];
    char csrf_token[CS_SESSION_CSRF_TOKEN_HEX_LEN + 1];
    char response_body[256];
    size_t body_len = 0;
    int consume_status;
    int throttled = 0;
    int expired = 0;
    int trusted_count = 0;
    int code_present = 0;
    int qr_present = 0;
    int written;

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_app_pairing_available(app)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false,\"error\":\"pairing_unavailable\"}");
    }

    if (cs_read_body(conn, body, sizeof(body), &body_len) != 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }

    if (mg_get_var(body, body_len, "browser_id", browser_id, sizeof(browser_id)) < 0) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }
    code_present = mg_get_var(body, body_len, "code", code, sizeof(code)) >= 0;
    qr_present = mg_get_var(body, body_len, "qr_token", qr_token, sizeof(qr_token)) >= 0;
    if (!code_present && !qr_present) {
        return cs_write_json(conn, 400, "Bad Request", "{\"ok\":false}");
    }

    if (qr_present) {
        consume_status = cs_server_consume_qr_pair_token(qr_token, &expired);
        if (consume_status < 0) {
            return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
        }
        if (expired) {
            return cs_write_json(conn, 410, "Gone", "{\"ok\":false,\"error\":\"qr_expired\"}");
        }
        if (consume_status == 0) {
            return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false,\"error\":\"invalid_qr_token\"}");
        }
    } else {
        consume_status = cs_server_consume_pairing_code(code, &throttled);
        if (consume_status < 0) {
            return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
        }
        if (throttled) {
            return cs_write_json(conn, 429, "Too Many Requests", "{\"ok\":false,\"error\":\"pairing_throttled\"}");
        }
        if (consume_status == 0) {
            return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false,\"error\":\"invalid_code\"}");
        }
    }

    if (cs_server_trust_browser(browser_id, trust_token, sizeof(trust_token)) != 0
        || cs_server_make_session_cookie(set_cookie, sizeof(set_cookie), trust_token) != 0
        || cs_server_make_csrf_token(csrf_token, sizeof(csrf_token), trust_token) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }

    trusted_count = cs_server_get_trusted_count();
    written = snprintf(response_body,
                       sizeof(response_body),
                       "{\"ok\":true,\"paired\":true,\"csrf\":\"%s\",\"trustedCount\":%d,\"capabilities\":{\"terminal\":%s}}",
                       csrf_token,
                       trusted_count,
                       cs_terminal_feature_enabled(app) ? "true" : "false");
    if (written < 0 || (size_t) written >= sizeof(response_body)) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Set-Cookie: %s\r\n"
              "Content-Length: %zu\r\n"
              "\r\n"
              "%s",
              set_cookie,
              strlen(response_body),
              response_body);
    return 1;
}

int cs_route_pair_qr_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    const struct mg_request_info *request = mg_get_request_info(conn);
    char token[64];
    char location[128];

    if (!cs_method_is(conn, "GET")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!app) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_app_pairing_available(app)) {
        return cs_write_html(conn,
                             403,
                             "Forbidden",
                             "Pairing unavailable",
                             "Reopen the app on the handheld to pair or change settings.");
    }
    if (!request || !request->query_string || mg_get_var(request->query_string, strlen(request->query_string), "token", token, sizeof(token)) < 0) {
        return cs_write_html(conn, 400, "Bad Request", "Missing QR token", "Open the pairing QR code on the handheld and scan it again.");
    }
    if (snprintf(location, sizeof(location), "/?pairQr=%s", token) >= (int) sizeof(location)) {
        return cs_write_html(conn, 500, "Internal Server Error", "QR pairing failed", "The handheld could not redirect this QR request.");
    }

    return cs_write_redirect(conn, location);
}

int cs_route_revoke_handler(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");
    const char *csrf = mg_get_header(conn, "X-CS-CSRF");
    char clear_cookie[128];

    if (!cs_method_is(conn, "POST")) {
        return cs_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    if (!cbdata) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_csrf_is_valid(cookie, csrf)) {
        return cs_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    if (cs_server_make_clear_cookie(clear_cookie, sizeof(clear_cookie)) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }
    if (cs_server_revoke_browser(cookie) != 0) {
        return cs_write_json(conn, 500, "Internal Server Error", "{\"ok\":false}");
    }
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              CS_SERVER_SECURITY_HEADERS_HTTP
              "Cache-Control: no-store\r\n"
              "Set-Cookie: %s\r\n"
              "Content-Length: 11\r\n"
              "\r\n"
              "{\"ok\":true}",
              clear_cookie);
    return 1;
}
