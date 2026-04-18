#include "cs_app.h"
#include "cs_dotclean.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"

#include "civetweb.h"

#include <stdlib.h>

int cs_route_mac_dotfiles_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_dotclean_entry *entries = NULL;
    size_t entry_count = 0;
    int truncated = 0;
    size_t i;
    int guard_status;

    if (!cs_routes_method_is(conn, "GET")) {
        return cs_routes_write_json(conn, 405, "Method Not Allowed", "{\"error\":\"method_not_allowed\"}");
    }
    guard_status = cs_routes_guard_get_strict(conn, cbdata);
    if (guard_status != 0) {
        return guard_status;
    }

    entries = (cs_dotclean_entry *) calloc(CS_DOTCLEAN_MAX_ENTRIES, sizeof(*entries));
    if (!entries) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"alloc_failed\"}");
    }
    if (cs_dotclean_scan(&app->paths, entries, CS_DOTCLEAN_MAX_ENTRIES, &entry_count, &truncated) != 0) {
        free(entries);
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"dotclean_scan_failed\"}");
    }

    if (cs_routes_stream_begin_json_response(conn) != 0) {
        free(entries);
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"count\":") != 0
        || cs_routes_stream_unsigned(conn, (unsigned long long) entry_count) != 0
        || cs_routes_stream_literal(conn, ",\"truncated\":") != 0
        || cs_routes_stream_literal(conn, truncated ? "true" : "false") != 0
        || cs_routes_stream_literal(conn, ",\"entries\":[") != 0) {
        goto stream_fail;
    }

    for (i = 0; i < entry_count && i < CS_DOTCLEAN_MAX_ENTRIES; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            goto stream_fail;
        }
        if (cs_routes_stream_literal(conn, "{\"path\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entries[i].path) != 0
            || cs_routes_stream_literal(conn, "\",\"kind\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entries[i].kind) != 0
            || cs_routes_stream_literal(conn, "\",\"reason\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entries[i].reason) != 0
            || cs_routes_stream_literal(conn, "\",\"size\":") != 0
            || cs_routes_stream_unsigned(conn, entries[i].size) != 0
            || cs_routes_stream_literal(conn, ",\"modified\":") != 0
            || cs_routes_stream_signed(conn, entries[i].modified) != 0
            || cs_routes_stream_literal(conn, "}") != 0) {
            goto stream_fail;
        }
    }

    if (cs_routes_stream_literal(conn, "]}") != 0 || mg_send_chunk(conn, "", 0) < 0) {
        goto stream_fail;
    }

    free(entries);
    return 1;

stream_fail:
    free(entries);
    (void) mg_send_chunk(conn, "", 0);
    return 1;
}
