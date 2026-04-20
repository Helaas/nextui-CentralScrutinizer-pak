#include "cs_app.h"
#include "cs_build_info.h"
#include "cs_routes_helpers.h"
#include "cs_session.h"
#include "cs_server.h"
#include "cs_terminal.h"

#include "civetweb.h"

#include <stdio.h>
int cs_route_status_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[256];
    int trusted_count = cs_server_get_trusted_count();
    int written;

    if (!cs_routes_method_is(conn, "GET")) {
        return cs_routes_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }

    written = snprintf(body,
                       sizeof(body),
                       "{\"platform\":\"%s\",\"port\":%d,\"trustedCount\":%d}",
                       cs_build_info_platform_name(),
                       app ? app->port : 0,
                       trusted_count);
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"status_too_large\"}");
    }

    return cs_routes_write_json(conn, 200, "OK", body);
}

int cs_route_session_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    char body[256];
    char cookie_token[64];
    char csrf_token[CS_SESSION_CSRF_TOKEN_HEX_LEN + 1];
    const char *cookie;
    int is_paired;
    int pairing_available;
    int trusted_count;
    int written;

    if (!cs_routes_method_is(conn, "GET")) {
        return cs_routes_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }

    cookie = mg_get_header(conn, "Cookie");
    is_paired = cs_server_cookie_is_valid(cookie);
    pairing_available = cs_app_pairing_available(app);
    trusted_count = cs_server_get_trusted_count();
    if (!is_paired) {
        written = snprintf(body,
                           sizeof(body),
                           cs_terminal_feature_enabled(app)
                               ? "{\"paired\":false,\"csrf\":null,\"trustedCount\":%d,\"pairingAvailable\":%s,\"capabilities\":{\"terminal\":true}}"
                               : "{\"paired\":false,\"csrf\":null,\"trustedCount\":%d,\"pairingAvailable\":%s,\"capabilities\":{\"terminal\":false}}",
                           trusted_count,
                           pairing_available ? "true" : "false");
        if (written < 0 || (size_t) written >= sizeof(body)) {
            return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"session_too_large\"}");
        }
        return cs_routes_write_json(conn,
                                    200,
                                    "OK",
                                    body);
    }
    if (cs_server_copy_cookie_token(cookie, cookie_token, sizeof(cookie_token)) != 0
        || cs_server_make_csrf_token(csrf_token, sizeof(csrf_token), cookie_token) != 0) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"session_too_large\"}");
    }

    written = snprintf(body,
                       sizeof(body),
                       "{\"paired\":true,\"csrf\":\"%s\",\"trustedCount\":%d,\"pairingAvailable\":%s,\"capabilities\":{\"terminal\":%s}}",
                       csrf_token,
                       trusted_count,
                       pairing_available ? "true" : "false",
                       cs_terminal_feature_enabled(app) ? "true" : "false");
    if (written < 0 || (size_t) written >= sizeof(body)) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"session_too_large\"}");
    }

    return cs_routes_write_json(conn, 200, "OK", body);
}
