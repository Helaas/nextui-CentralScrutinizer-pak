#include "cs_app.h"
#include "cs_platforms.h"
#include "cs_routes_helpers.h"
#include "cs_server.h"
#include "cs_states.h"

#include "civetweb.h"

#include <stdlib.h>
#include <string.h>

static int cs_stream_string_array(struct mg_connection *conn,
                                  const char values[][CS_PATH_MAX],
                                  size_t count) {
    size_t i;

    if (!conn) {
        return -1;
    }

    for (i = 0; i < count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "\"") != 0 || cs_routes_stream_escaped_string(conn, values[i]) != 0
            || cs_routes_stream_literal(conn, "\"") != 0) {
            return -1;
        }
    }

    return 0;
}

static int cs_stream_warning_array(struct mg_connection *conn, const cs_state_entry *entry) {
    size_t i;

    if (!conn || !entry) {
        return -1;
    }

    for (i = 0; i < entry->warning_count; ++i) {
        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            return -1;
        }
        if (cs_routes_stream_literal(conn, "\"") != 0 || cs_routes_stream_escaped_string(conn, entry->warnings[i]) != 0
            || cs_routes_stream_literal(conn, "\"") != 0) {
            return -1;
        }
    }

    return 0;
}

int cs_route_states_handler(struct mg_connection *conn, void *cbdata) {
    const struct mg_request_info *request = mg_get_request_info(conn);
    cs_app *app = (cs_app *) cbdata;
    char tag_value[64];
    cs_platform_info platform = {0};
    cs_state_entry *entries = NULL;
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
    if (!request || !request->query_string
        || mg_get_var(request->query_string, strlen(request->query_string), "tag", tag_value, sizeof(tag_value)) <= 0) {
        return cs_routes_write_json(conn, 400, "Bad Request", "{\"error\":\"missing_tag\"}");
    }
    if (cs_platform_resolve(&app->paths, tag_value, &platform) != 0) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"platform_not_found\"}");
    }
    if (!cs_platform_supports_resource(&platform, "states")) {
        return cs_routes_write_json(conn, 404, "Not Found", "{\"error\":\"states_not_supported\"}");
    }

    entries = (cs_state_entry *) calloc(CS_STATE_MAX_ENTRIES, sizeof(*entries));
    if (!entries) {
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"alloc_failed\"}");
    }
    if (cs_states_collect(&app->paths, &platform, entries, CS_STATE_MAX_ENTRIES, &entry_count, &truncated) != 0) {
        free(entries);
        return cs_routes_write_json(conn, 500, "Internal Server Error", "{\"error\":\"state_scan_failed\"}");
    }

    if (cs_routes_stream_begin_json_response(conn) != 0) {
        free(entries);
        return 1;
    }
    if (cs_routes_stream_literal(conn, "{\"platformTag\":\"") != 0
        || cs_routes_stream_escaped_string(conn, platform.tag) != 0
        || cs_routes_stream_literal(conn, "\",\"platformName\":\"") != 0
        || cs_routes_stream_escaped_string(conn, platform.name) != 0
        || cs_routes_stream_literal(conn, "\",\"emuCode\":\"") != 0
        || cs_routes_stream_escaped_string(conn, platform.primary_code) != 0
        || cs_routes_stream_literal(conn, "\",\"count\":") != 0
        || cs_routes_stream_unsigned(conn, (unsigned long long) entry_count) != 0
        || cs_routes_stream_literal(conn, ",\"truncated\":") != 0
        || cs_routes_stream_literal(conn, truncated ? "true" : "false") != 0
        || cs_routes_stream_literal(conn, ",\"entries\":[") != 0) {
        goto stream_fail;
    }

    for (i = 0; i < entry_count && i < CS_STATE_MAX_ENTRIES; ++i) {
        const cs_state_entry *entry = &entries[i];

        if (i > 0 && cs_routes_stream_literal(conn, ",") != 0) {
            goto stream_fail;
        }
        if (cs_routes_stream_literal(conn, "{\"id\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->id) != 0
            || cs_routes_stream_literal(conn, "\",\"title\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->title) != 0
            || cs_routes_stream_literal(conn, "\",\"coreDir\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->core_dir) != 0
            || cs_routes_stream_literal(conn, "\",\"slot\":") != 0
            || cs_routes_stream_signed(conn, entry->slot) != 0
            || cs_routes_stream_literal(conn, ",\"slotLabel\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->slot_label) != 0
            || cs_routes_stream_literal(conn, "\",\"kind\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->kind) != 0
            || cs_routes_stream_literal(conn, "\",\"format\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->format) != 0
            || cs_routes_stream_literal(conn, "\",\"modified\":") != 0
            || cs_routes_stream_signed(conn, entry->modified) != 0
            || cs_routes_stream_literal(conn, ",\"size\":") != 0
            || cs_routes_stream_unsigned(conn, entry->size) != 0
            || cs_routes_stream_literal(conn, ",\"previewPath\":\"") != 0
            || cs_routes_stream_escaped_string(conn, entry->preview_path) != 0
            || cs_routes_stream_literal(conn, "\",\"downloadPaths\":[") != 0
            || cs_stream_string_array(conn, entry->download_paths, entry->download_path_count) != 0
            || cs_routes_stream_literal(conn, "],\"deletePaths\":[") != 0
            || cs_stream_string_array(conn, entry->delete_paths, entry->delete_path_count) != 0
            || cs_routes_stream_literal(conn, "],\"warnings\":[") != 0
            || cs_stream_warning_array(conn, entry) != 0
            || cs_routes_stream_literal(conn, "]}") != 0) {
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
