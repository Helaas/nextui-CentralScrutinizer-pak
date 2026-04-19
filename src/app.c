#include "cs_app.h"
#include "cs_daemon.h"
#include "cs_settings.h"
#include "cs_server.h"
#include "cs_terminal.h"
#include "cs_ui.h"
#include "cs_util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define CS_APP_DAEMON_READY_OK '1'
#define CS_APP_DAEMON_READY_FAIL '0'
#define CS_APP_DAEMON_START_SIGNAL '1'
#define CS_APP_DAEMON_READY_TIMEOUT_MS 10000

int cs_app_get_terminal_enabled(const cs_app *app) {
    return app ? atomic_load_explicit((atomic_int *) &app->terminal_enabled, memory_order_relaxed) : 0;
}

int cs_app_can_background(const cs_app *app) {
    return app && cs_server_get_trusted_count() > 0;
}

int cs_app_pairing_available(const cs_app *app) {
    return app && !app->daemonized;
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
            "Usage: %s [--headless] [--daemonized] [--port <port>] [--web-root <path>] [--sdcard <path>]\n",
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

static int cs_parse_fd(const char *value, int *fd_out) {
    char *end = NULL;
    long parsed;

    if (!value || !fd_out) {
        return -1;
    }

    parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= STDERR_FILENO || parsed > 65535) {
        return -1;
    }

    *fd_out = (int) parsed;
    return 0;
}

static void cs_app_write_daemon_ready_status(cs_app *app, char status) {
    ssize_t written;

    if (!app || app->daemon_ready_fd < 0) {
        return;
    }

    do {
        written = write(app->daemon_ready_fd, &status, 1);
    } while (written < 0 && errno == EINTR);

    close(app->daemon_ready_fd);
    app->daemon_ready_fd = -1;
}

static int cs_app_wait_for_daemon_start_signal(cs_app *app) {
    char signal_byte = '\0';
    ssize_t nread;

    if (!app || app->daemon_start_fd < 0) {
        return 0;
    }

    do {
        nread = read(app->daemon_start_fd, &signal_byte, 1);
    } while (nread < 0 && errno == EINTR);

    close(app->daemon_start_fd);
    app->daemon_start_fd = -1;
    return nread == 1 && signal_byte == CS_APP_DAEMON_START_SIGNAL ? 0 : -1;
}

static int cs_app_wait_for_daemon_ready(int fd, int timeout_ms) {
    struct pollfd pfd;
    char status = '\0';
    ssize_t nread;
    int rc;

    if (fd < 0 || timeout_ms < 0) {
        return -1;
    }

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN | POLLHUP;

    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    if (rc <= 0) {
        return -1;
    }

    do {
        nread = read(fd, &status, 1);
    } while (nread < 0 && errno == EINTR);
    return nread == 1 && status == CS_APP_DAEMON_READY_OK ? 0 : -1;
}

static void cs_app_child_exec_failed(int ready_fd) {
    const char status = CS_APP_DAEMON_READY_FAIL;

    if (ready_fd >= 0) {
        (void) write(ready_fd, &status, 1);
    }
    _exit(1);
}

static pid_t cs_app_spawn_daemon_child(const cs_app *app, int ready_fd, int start_fd) {
    char port_arg[16];
    char ready_fd_arg[16];
    char start_fd_arg[16];
    int devnull_fd;
    char *const argv[] = {(char *) app->argv0,
                          "--headless",
                          "--daemonized",
                          "--port",
                          port_arg,
                          "--daemon-ready-fd",
                          ready_fd_arg,
                          "--daemon-start-fd",
                          start_fd_arg,
                          NULL};
    pid_t pid;

    if (!app || app->argv0[0] == '\0' || ready_fd < 0 || start_fd < 0) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(port_arg, sizeof(port_arg), "%d", app->port) != 0
        || CS_SAFE_SNPRINTF(ready_fd_arg, sizeof(ready_fd_arg), "%d", ready_fd) != 0
        || CS_SAFE_SNPRINTF(start_fd_arg, sizeof(start_fd_arg), "%d", start_fd) != 0) {
        return -1;
    }

    pid = fork();
    if (pid != 0) {
        return pid;
    }

    if (setsid() < 0) {
        cs_app_child_exec_failed(ready_fd);
    }
    devnull_fd = open("/dev/null", O_RDWR);
    if (devnull_fd < 0 || dup2(devnull_fd, STDIN_FILENO) < 0 || dup2(devnull_fd, STDOUT_FILENO) < 0
        || dup2(devnull_fd, STDERR_FILENO) < 0) {
        if (devnull_fd > STDERR_FILENO) {
            close(devnull_fd);
        }
        cs_app_child_exec_failed(ready_fd);
    }
    if (devnull_fd > STDERR_FILENO) {
        close(devnull_fd);
    }
    execvp(app->argv0, argv);
    cs_app_child_exec_failed(ready_fd);
    return -1;
}

static int cs_app_start_background_server(cs_app *app) {
    int ready_pipe[2] = {-1, -1};
    int start_pipe[2] = {-1, -1};
    pid_t child_pid = -1;
    int rc = -1;
    char start_signal = CS_APP_DAEMON_START_SIGNAL;

    if (!app) {
        return -1;
    }
    if (!cs_app_can_background(app)) {
        cs_ui_show_error("Run in Background requires at least one trusted browser.");
        return 0;
    }
    if (pipe(ready_pipe) != 0 || pipe(start_pipe) != 0) {
        if (ready_pipe[0] >= 0) {
            close(ready_pipe[0]);
        }
        if (ready_pipe[1] >= 0) {
            close(ready_pipe[1]);
        }
        if (start_pipe[0] >= 0) {
            close(start_pipe[0]);
        }
        if (start_pipe[1] >= 0) {
            close(start_pipe[1]);
        }
        cs_ui_show_error("Could not prepare background mode.");
        return 0;
    }
    (void) fcntl(ready_pipe[0], F_SETFD, FD_CLOEXEC);
    (void) fcntl(start_pipe[1], F_SETFD, FD_CLOEXEC);

    child_pid = cs_app_spawn_daemon_child(app, ready_pipe[1], start_pipe[0]);
    close(ready_pipe[1]);
    ready_pipe[1] = -1;
    close(start_pipe[0]);
    start_pipe[0] = -1;
    if (child_pid < 0) {
        close(ready_pipe[0]);
        close(start_pipe[1]);
        cs_ui_show_error("Could not start the background server.");
        return 0;
    }

    cs_terminal_manager_close_all(app, "{\"type\":\"error\",\"error\":\"server_backgrounded\"}");
    cs_server_stop();

    if (write(start_pipe[1], &start_signal, 1) != 1) {
        goto background_failed;
    }
    close(start_pipe[1]);
    start_pipe[1] = -1;

    if (cs_app_wait_for_daemon_ready(ready_pipe[0], CS_APP_DAEMON_READY_TIMEOUT_MS) == 0) {
        close(ready_pipe[0]);
        return 1;
    }

background_failed:
    if (ready_pipe[0] >= 0) {
        close(ready_pipe[0]);
    }
    if (start_pipe[1] >= 0) {
        close(start_pipe[1]);
    }
    if (child_pid > 0) {
        (void) kill(child_pid, SIGKILL);
        (void) waitpid(child_pid, NULL, 0);
    }

    if (cs_server_start(app) == 0) {
        cs_ui_show_error("Could not start background mode. The app will stay in the foreground.");
        return 0;
    }

    cs_ui_show_error("Background mode failed and the foreground server could not be restored.");
    return rc;
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

        if (strcmp(argv[i], "--daemonized") == 0) {
            app->headless = 1;
            app->daemonized = 1;
            continue;
        }

        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc || cs_parse_port(argv[i + 1], &app->port) != 0) {
                return -1;
            }
            app->port_explicitly_set = 1;
            ++i;
            continue;
        }

        if (strcmp(argv[i], "--daemon-ready-fd") == 0) {
            if (i + 1 >= argc || cs_parse_fd(argv[i + 1], &app->daemon_ready_fd) != 0) {
                return -1;
            }
            ++i;
            continue;
        }

        if (strcmp(argv[i], "--daemon-start-fd") == 0) {
            if (i + 1 >= argc || cs_parse_fd(argv[i + 1], &app->daemon_start_fd) != 0) {
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
    struct sigaction sa;

    if (!waitset || !oldset) {
        return -1;
    }

    if (sigemptyset(waitset) != 0 || sigaddset(waitset, SIGINT) != 0 || sigaddset(waitset, SIGTERM) != 0) {
        return -1;
    }

    if (sigprocmask(SIG_BLOCK, waitset, oldset) != 0) {
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigemptyset(&sa.sa_mask) != 0 || sigaction(SIGHUP, &sa, NULL) != 0) {
        (void) sigprocmask(SIG_SETMASK, oldset, NULL);
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
            case CS_UI_ACTION_BACKGROUND: {
                int background_result = cs_app_start_background_server(app);

                if (background_result > 0) {
                    server_started = 0;
                    cs_ui_shutdown();
                    return 0;
                }
                if (background_result < 0) {
                    server_started = 0;
                    cs_ui_shutdown();
                    return 1;
                }
                server_started = 1;
                continue;
            }
            default:
                if (server_started) {
                    cs_terminal_manager_close_all(app, "{\"type\":\"error\",\"error\":\"server_shutdown\"}");
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
    app.daemon_ready_fd = -1;
    app.daemon_start_fd = -1;
    if (CS_SAFE_SNPRINTF(app.argv0, sizeof(app.argv0), "%s", argv[0]) != 0) {
        fprintf(stderr, "Executable path is too long\n");
        return 1;
    }

    if (cs_app_parse_args(&app, argc, argv) != 0) {
        cs_app_usage(argv[0]);
        return 1;
    }

    if (cs_paths_init(&app.paths) != 0) {
        fprintf(stderr, "Failed to initialize paths\n");
        return 1;
    }
    if (!app.daemonized
        && cs_daemon_prepare_foreground_start(&app.paths, app.port, app.port_explicitly_set, &app.port) != 0) {
        fprintf(stderr, "Failed to stop existing background server\n");
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

    if (app.daemonized && cs_app_wait_for_daemon_start_signal(&app) != 0) {
        cs_app_write_daemon_ready_status(&app, CS_APP_DAEMON_READY_FAIL);
        cs_terminal_manager_shutdown(&app);
        fprintf(stderr, "Failed to receive daemon start signal\n");
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
            cs_app_write_daemon_ready_status(&app, CS_APP_DAEMON_READY_FAIL);
            if (has_oldset) {
                (void) sigprocmask(SIG_SETMASK, &oldset, NULL);
            }
            cs_terminal_manager_shutdown(&app);
            fprintf(stderr, "Failed to start HTTP server\n");
            return 1;
        }
        if (app.daemonized) {
            cs_daemon_state state = {.pid = getpid(), .port = app.port};

            if (!cs_app_can_background(&app)) {
                cs_app_write_daemon_ready_status(&app, CS_APP_DAEMON_READY_FAIL);
                cs_server_stop();
                cs_terminal_manager_shutdown(&app);
                if (has_oldset) {
                    (void) sigprocmask(SIG_SETMASK, &oldset, NULL);
                }
                fprintf(stderr, "Daemonized mode requires at least one trusted browser\n");
                return 1;
            }
            if (cs_daemon_state_save(&app.paths, &state) != 0) {
                cs_app_write_daemon_ready_status(&app, CS_APP_DAEMON_READY_FAIL);
                cs_server_stop();
                cs_terminal_manager_shutdown(&app);
                if (has_oldset) {
                    (void) sigprocmask(SIG_SETMASK, &oldset, NULL);
                }
                fprintf(stderr, "Failed to persist daemon state\n");
                return 1;
            }
        }
        cs_app_write_daemon_ready_status(&app, CS_APP_DAEMON_READY_OK);
        int loop_result = cs_app_run_headless_loop(&waitset);
        cs_server_stop();
        if (app.daemonized) {
            (void) cs_daemon_state_clear(&app.paths);
        }
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
