#include "cs_app.h"
#include "cs_dotclean.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"

#include "civetweb.h"

#include <stdlib.h>

/* Defensive ceiling to bound allocation and the amount of work the web UI/cleanup path must
 * process. On target handhelds (TG5040/TG5050/MY355, ~1 GB RAM) and over the in-process civetweb
 * server, surfacing tens of thousands of rows would OOM the app or hammer the single-threaded
 * server with concurrent deletes. The UI renders truncated=true and asks the user to clean the
 * first batch before re-scanning.
 */
#define CS_DOTCLEAN_HARD_CAP ((size_t) 10000)

int cs_route_mac_dotfiles_handler(struct mg_connection *conn, void *cbdata) {
    cs_app *app = (cs_app *) cbdata;
    cs_dotclean_entry *entries = NULL;
    size_t total_count = 0;
    size_t capacity = 0;
    size_t emit_count = 0;
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

    /* First pass: count matches without allocating storage. */
    if (cs_dotclean_scan(&app->paths, NULL, 0, &total_count, NULL) != 0) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"dotclean_scan_failed\"}");
    }
    capacity = total_count < CS_DOTCLEAN_HARD_CAP ? total_count : CS_DOTCLEAN_HARD_CAP;
    if (capacity > 0) {
        int scan_truncated = 0;

        entries = (cs_dotclean_entry *) calloc(capacity, sizeof(*entries));
        if (!entries) {
            return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"alloc_failed\"}");
        }
        /* Second pass: populate entries. The scanner always reports the full match count in
         * total_count, even if the filesystem changed between passes or the buffer is smaller
         * than the first-pass count. truncated_out flags when the buffer was too small.
         */
        if (cs_dotclean_scan(&app->paths, entries, capacity, &total_count, &scan_truncated) != 0) {
            free(entries);
            return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"dotclean_scan_failed\"}");
        }
        if (scan_truncated) {
            truncated = 1;
        }
    }
    emit_count = total_count < capacity ? total_count : capacity;

    if (cs_routes_stream_begin_json_response(conn) != 0) {
        free(entries);
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"count\":") != 0
        || cs_routes_stream_unsigned(conn, (unsigned long long) total_count) != 0
        || cs_routes_stream_literal(conn, ",\"truncated\":") != 0
        || cs_routes_stream_literal(conn, truncated ? "true" : "false") != 0
        || cs_routes_stream_literal(conn, ",\"entries\":[") != 0) {
        goto stream_fail;
    }

    for (i = 0; i < emit_count; ++i) {
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
