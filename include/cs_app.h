#ifndef CS_APP_H
#define CS_APP_H

#include <stdatomic.h>

#include "cs_paths.h"

struct cs_terminal_manager;

typedef struct cs_app {
    cs_paths paths;
    int port;
    int headless;
    atomic_int terminal_enabled;
    struct cs_terminal_manager *terminal_manager;
} cs_app;

int cs_app_get_terminal_enabled(const cs_app *app);
int cs_app_set_terminal_enabled(cs_app *app, int enabled);
int cs_app_run(int argc, char **argv);

#endif
