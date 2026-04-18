#include "cs_routes_helpers.h"
#include "cs_server.h"

#include <stdio.h>
#include <string.h>

int cs_routes_method_is(const struct mg_connection *conn, const char *method) {
    const struct mg_request_info *request = mg_get_request_info(conn);

    return request && request->request_method && strcmp(request->request_method, method) == 0;
}

int cs_routes_write_json(struct mg_connection *conn, int status, const char *reason, const char *body) {
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

int cs_routes_stream_begin_json_response(struct mg_connection *conn) {
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

int cs_routes_stream_literal(struct mg_connection *conn, const char *literal) {
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

int cs_routes_stream_escaped_string(struct mg_connection *conn, const char *value) {
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

int cs_routes_stream_unsigned(struct mg_connection *conn, unsigned long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%llu", value) < 0) {
        return -1;
    }

    return cs_routes_stream_literal(conn, buffer);
}

int cs_routes_stream_signed(struct mg_connection *conn, long long value) {
    char buffer[64];

    if (snprintf(buffer, sizeof(buffer), "%lld", value) < 0) {
        return -1;
    }

    return cs_routes_stream_literal(conn, buffer);
}

int cs_routes_guard_get_strict(struct mg_connection *conn, void *cbdata) {
    const char *cookie = mg_get_header(conn, "Cookie");

    if (!cbdata) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"missing_app\"}");
    }
    if (!cs_server_cookie_is_valid(cookie) || !cs_server_request_csrf_is_valid(conn, 0)) {
        return cs_routes_write_json(conn, 403, "Forbidden", "{\"ok\":false}");
    }

    return 0;
}
