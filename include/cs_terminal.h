#ifndef CS_TERMINAL_H
#define CS_TERMINAL_H

struct cs_app;
struct mg_connection;
struct mg_context;

typedef struct cs_terminal_manager cs_terminal_manager;

int cs_terminal_manager_init(struct cs_app *app);
void cs_terminal_manager_shutdown(struct cs_app *app);
void cs_terminal_manager_close_all(struct cs_app *app, const char *reason);
int cs_terminal_feature_enabled(const struct cs_app *app);
int cs_terminal_route_session_handler(struct mg_connection *conn, void *cbdata);
void cs_terminal_register_websocket(struct mg_context *ctx, struct cs_app *app);

#endif
