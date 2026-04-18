#ifndef CS_UI_H
#define CS_UI_H

typedef enum cs_ui_action {
    CS_UI_ACTION_EXIT = 0,
    CS_UI_ACTION_REFRESH = 1,
    CS_UI_ACTION_REVOKE = 2,
} cs_ui_action;

typedef struct cs_ui_model {
    char ip[64];
    int port;
    char code[8];
    int trusted_browser_count;
    int terminal_enabled;
    int is_offline;
    char status_message[128];
} cs_ui_model;

struct cs_app;

void cs_ui_model_make_active(
    cs_ui_model *model, const char *ip, int port, const char *code, int trusted_count, int terminal_enabled);
void cs_ui_model_make_offline(cs_ui_model *model);
int cs_ui_init(void);
void cs_ui_shutdown(void);
int cs_ui_run_server_screen(struct cs_app *app, const cs_ui_model *model);

#endif
