#include "cs_app.h"
#include "cs_settings.h"
#include "cs_server.h"
#include "cs_terminal.h"
#include "cs_ui.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int cs_app_get_terminal_enabled(const cs_app *app) {
    return app ? atomic_load_explicit((atomic_int *) &app->terminal_enabled, memory_order_relaxed) : 0;
}

int cs_app_set_terminal_enabled(cs_app *app, int enabled) {
    cs_settings settings = {0};
    int normalized;

    if (!app) {
        return -1;
    }

    normalized = enabled ? 1 : 0;
    settings.terminal_enabled = normalized;
    if (cs_settings_save(&app->paths, &settings) != 0) {
        return -1;
    }

    atomic_store_explicit(&app->terminal_enabled, normalized, memory_order_relaxed);
    if (!normalized) {
        cs_terminal_manager_close_all(app, "{\"type\":\"error\",\"error\":\"terminal_disabled\"}");
    }

    return 0;
}

static void cs_app_usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s [--headless] [--port <port>] [--web-root <path>] [--sdcard <path>]\n",
            argv0);
}

static int cs_parse_port(const char *value, int *port_out) {
    char *end = NULL;
    long parsed;

    if (!value || !port_out) {
        return -1;
    }

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1 || parsed > 65535) {
        return -1;
    }

    *port_out = (int) parsed;
    return 0;
}

static int cs_app_find_device_ip(char *buffer, size_t buffer_len) {
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    const char *override = getenv("CS_DEVICE_IP");

    if (!buffer || buffer_len == 0) {
        return -1;
    }

    if (override && override[0] != '\0') {
        return snprintf(buffer, buffer_len, "%s", override) < (int) buffer_len ? 0 : -1;
    }

#if defined(PLATFORM_MAC)
    if (snprintf(buffer, buffer_len, "%s", "127.0.0.1") >= (int) buffer_len) {
        return -1;
    }
    return 0;
#else
    if (getifaddrs(&ifaddr) != 0) {
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        if (!inet_ntop(AF_INET,
                       &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr,
                       buffer,
                       (socklen_t) buffer_len)) {
            continue;
        }
        freeifaddrs(ifaddr);
        return 0;
    }

    freeifaddrs(ifaddr);
    return -1;
#endif
}

static int cs_start_server_with_fallback(cs_app *app, int preferred) {
    int candidates[] = {preferred, 8878, 8879, 8880};
    size_t i;
    size_t j;

    if (!app) {
        return -1;
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        int duplicate = 0;

        for (j = 0; j < i; ++j) {
            if (candidates[i] == candidates[j]) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            app->port = candidates[i];
            if (cs_server_start(app) == 0) {
                return 0;
            }
        }
    }

    return -1;
}

static int cs_app_parse_args(cs_app *app, int argc, char **argv) {
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            app->headless = 1;
            continue;
        }

        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc || cs_parse_port(argv[i + 1], &app->port) != 0) {
                return -1;
            }
            ++i;
            continue;
        }

        if (strcmp(argv[i], "--web-root") == 0) {
            if (i + 1 >= argc || setenv("CS_WEB_ROOT", argv[i + 1], 1) != 0) {
                return -1;
            }
            ++i;
            continue;
        }

        if (strcmp(argv[i], "--sdcard") == 0) {
            if (i + 1 >= argc || setenv("SDCARD_PATH", argv[i + 1], 1) != 0) {
                return -1;
            }
            ++i;
            continue;
        }

        return -1;
    }

    return 0;
}

static int cs_app_block_headless_signals(sigset_t *waitset, sigset_t *oldset) {
    if (!waitset || !oldset) {
        return -1;
    }

    if (sigemptyset(waitset) != 0 || sigaddset(waitset, SIGINT) != 0 || sigaddset(waitset, SIGTERM) != 0) {
        return -1;
    }

    if (sigprocmask(SIG_BLOCK, waitset, oldset) != 0) {
        return -1;
    }

    return 0;
}

static int cs_app_run_headless_loop(const sigset_t *waitset) {
    int signal_number = 0;

    if (!waitset) {
        return -1;
    }

    return sigwait(waitset, &signal_number) == 0 ? 0 : -1;
}

static int cs_app_run_ui(cs_app *app) {
    cs_ui_model model;
    char ip[64];
    char pairing_code[8];
    int server_started = 0;

    if (!app) {
        return 1;
    }

    if (cs_ui_init() != 0) {
        fprintf(stderr, "Failed to initialize handheld UI\n");
        return 1;
    }

    for (;;) {
        if (cs_app_find_device_ip(ip, sizeof(ip)) != 0) {
            if (server_started) {
                cs_server_stop();
                server_started = 0;
            }
            cs_ui_model_make_offline(&model);
        } else {
            if (!server_started) {
                if (cs_start_server_with_fallback(app, app->port) != 0) {
                    fprintf(stderr, "Failed to start HTTP server on any fallback port\n");
                    cs_ui_shutdown();
                    return 1;
                }
                server_started = 1;
            }
            pairing_code[0] = '\0';
            (void) cs_server_copy_pairing_code(pairing_code, sizeof(pairing_code));
            cs_ui_model_make_active(&model,
                                    ip,
                                    app->port,
                                    pairing_code,
                                    cs_server_get_trusted_count(),
                                    cs_app_get_terminal_enabled(app));
        }

        switch (cs_ui_run_server_screen(app, &model)) {
            case CS_UI_ACTION_REFRESH:
                continue;
            case CS_UI_ACTION_REVOKE:
                (void) cs_server_reset_session();
                continue;
            default:
                if (server_started) {
                    cs_server_stop();
                }
                cs_ui_shutdown();
                return 0;
        }
    }
}

int cs_app_run(int argc, char **argv) {
    cs_app app;
    cs_settings settings = {0};
    sigset_t waitset;
    sigset_t oldset;
    int has_oldset = 0;

    memset(&app, 0, sizeof(app));
    app.port = 8877;

    if (cs_app_parse_args(&app, argc, argv) != 0) {
        cs_app_usage(argv[0]);
        return 1;
    }

    if (cs_paths_init(&app.paths) != 0) {
        fprintf(stderr, "Failed to initialize paths\n");
        return 1;
    }
    settings.terminal_enabled = cs_settings_default_terminal_enabled();
    if (cs_settings_load(&app.paths, &settings) != 0) {
        fprintf(stderr, "Failed to load settings, using defaults\n");
    }
    atomic_init(&app.terminal_enabled, settings.terminal_enabled ? 1 : 0);
    if (cs_terminal_manager_init(&app) != 0) {
        fprintf(stderr, "Failed to initialize terminal manager\n");
        return 1;
    }

    if (app.headless) {
        if (cs_app_block_headless_signals(&waitset, &oldset) != 0) {
            cs_terminal_manager_shutdown(&app);
            fprintf(stderr, "Failed to prepare signal handling\n");
            return 1;
        }
        has_oldset = 1;
    }

    if (app.headless) {
        if (cs_server_start(&app) != 0) {
            if (has_oldset) {
                (void) sigprocmask(SIG_SETMASK, &oldset, NULL);
            }
            cs_terminal_manager_shutdown(&app);
            fprintf(stderr, "Failed to start HTTP server\n");
            return 1;
        }
        int loop_result = cs_app_run_headless_loop(&waitset);
        cs_server_stop();
        cs_terminal_manager_shutdown(&app);
        if (has_oldset) {
            (void) sigprocmask(SIG_SETMASK, &oldset, NULL);
        }
        return loop_result == 0 ? 0 : 1;
    }

    {
        int result = cs_app_run_ui(&app);

        cs_terminal_manager_shutdown(&app);
        return result;
    }
}
