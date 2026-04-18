#ifndef CS_ROUTES_HELPERS_H
#define CS_ROUTES_HELPERS_H

#include "civetweb.h"

int cs_routes_method_is(const struct mg_connection *conn, const char *method);
int cs_routes_write_json(struct mg_connection *conn, int status, const char *reason, const char *body);
int cs_routes_stream_begin_json_response(struct mg_connection *conn);
int cs_routes_stream_literal(struct mg_connection *conn, const char *literal);
int cs_routes_stream_escaped_string(struct mg_connection *conn, const char *value);
int cs_routes_stream_unsigned(struct mg_connection *conn, unsigned long long value);
int cs_routes_stream_signed(struct mg_connection *conn, long long value);
int cs_routes_guard_get_strict(struct mg_connection *conn, void *cbdata);

#endif
