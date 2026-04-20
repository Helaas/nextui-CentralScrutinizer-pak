#include "cs_daemon.h"

#include "cs_keep_awake.h"
#include "cs_util.h"

#include "../third_party/jsmn/jsmn.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int cs_daemon_ensure_parent_dir(const char *path) {
    char parent[CS_PATH_MAX];
    size_t i;

    if (!path) {
        return -1;
    }

    if (CS_SAFE_SNPRINTF(parent, sizeof(parent), "%s", path) != 0) {
        return -1;
    }

    for (i = strlen(parent); i > 0; --i) {
        if (parent[i - 1] == '/') {
            parent[i - 1] = '\0';
            break;
        }
    }
    if (i == 0 || parent[0] == '\0') {
        return -1;
    }

    for (i = 1; parent[i] != '\0'; ++i) {
        if (parent[i] != '/') {
            continue;
        }
        parent[i] = '\0';
        if (mkdir(parent, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        parent[i] = '/';
    }

    if (mkdir(parent, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int cs_daemon_token_eq(const char *json, const jsmntok_t *token, const char *expected) {
    size_t expected_len = strlen(expected);
    size_t token_len;

    if (!json || !token || !expected || token->type != JSMN_STRING) {
        return 0;
    }

    token_len = (size_t) (token->end - token->start);
    return token_len == expected_len && strncmp(json + token->start, expected, token_len) == 0;
}

static int cs_daemon_copy_long(long long *out, const char *json, const jsmntok_t *token) {
    char number[32];
    size_t token_len;
    char *end = NULL;
    long long value;

    if (!out || !json || !token || token->type != JSMN_PRIMITIVE) {
        return -1;
    }

    token_len = (size_t) (token->end - token->start);
    if (token_len == 0 || token_len >= sizeof(number)) {
        return -1;
    }

    memcpy(number, json + token->start, token_len);
    number[token_len] = '\0';
    errno = 0;
    value = strtoll(number, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }

    *out = value;
    return 0;
}

static int cs_daemon_can_bind_port(int port) {
    struct sockaddr_in addr;
    int fd;
    int on = 1;
    int rc;

    if (port < 1 || port > 65535) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t) port);

    rc = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
    close(fd);
    if (rc == 0) {
        return 1;
    }
    if (errno == EADDRINUSE) {
        return 0;
    }
    return -1;
}

int cs_daemon_state_make_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    if (!paths || !buffer || buffer_len == 0) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(buffer, buffer_len, "%s/daemon-state.json", paths->shared_state_root);
}

int cs_daemon_state_load(const cs_paths *paths, cs_daemon_state *state) {
    char path[CS_PATH_MAX];
    FILE *fp = NULL;
    long file_size;
    char *json = NULL;
    jsmn_parser parser;
    jsmntok_t tokens[16];
    int token_count;
    long long pid_value = 0;
    long long port_value = 0;
    int seen_pid = 0;
    int seen_port = 0;
    int i;

    if (!paths || !state) {
        return -1;
    }

    memset(state, 0, sizeof(*state));
    if (cs_daemon_state_make_path(paths, path, sizeof(path)) != 0) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return errno == ENOENT ? 0 : -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    file_size = ftell(fp);
    if (file_size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    json = (char *) malloc((size_t) file_size + 1u);
    if (!json) {
        fclose(fp);
        return -1;
    }
    if (fread(json, 1, (size_t) file_size, fp) != (size_t) file_size) {
        free(json);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    json[file_size] = '\0';

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, (size_t) file_size, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 5 || tokens[0].type != JSMN_OBJECT || tokens[0].size != 2) {
        free(json);
        return -1;
    }

    for (i = 1; i + 1 < token_count; i += 2) {
        if (cs_daemon_token_eq(json, &tokens[i], "pid")) {
            if (seen_pid || cs_daemon_copy_long(&pid_value, json, &tokens[i + 1]) != 0) {
                free(json);
                return -1;
            }
            seen_pid = 1;
            continue;
        }
        if (cs_daemon_token_eq(json, &tokens[i], "port")) {
            if (seen_port || cs_daemon_copy_long(&port_value, json, &tokens[i + 1]) != 0) {
                free(json);
                return -1;
            }
            seen_port = 1;
            continue;
        }

        free(json);
        return -1;
    }

    free(json);
    if (!seen_pid || !seen_port || pid_value <= 0 || port_value < 1 || port_value > 65535) {
        return -1;
    }

    state->pid = (pid_t) pid_value;
    state->port = (int) port_value;
    return 0;
}

int cs_daemon_state_save(const cs_paths *paths, const cs_daemon_state *state) {
    char path[CS_PATH_MAX];
    char temp_path[CS_PATH_MAX];
    FILE *fp = NULL;
    int fd;

    if (!paths || !state || state->pid <= 0 || state->port < 1 || state->port > 65535) {
        return -1;
    }
    if (cs_daemon_state_make_path(paths, path, sizeof(path)) != 0) {
        return -1;
    }
    if (cs_daemon_ensure_parent_dir(path) != 0) {
        return -1;
    }
    if (strlen(path) + sizeof(".tmpXXXXXX") > sizeof(temp_path)) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(temp_path, sizeof(temp_path), "%s.tmpXXXXXX", path) != 0) {
        return -1;
    }

    fd = mkstemp(temp_path);
    if (fd < 0) {
        return -1;
    }

    fp = fdopen(fd, "wb");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        return -1;
    }

    if (fprintf(fp, "{\"pid\":%lld,\"port\":%d}", (long long) state->pid, state->port) < 0
        || fflush(fp) != 0
        || fsync(fileno(fp)) != 0) {
        fclose(fp);
        unlink(temp_path);
        return -1;
    }
    if (fclose(fp) != 0) {
        unlink(temp_path);
        return -1;
    }

    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }

    return 0;
}

int cs_daemon_state_clear(const cs_paths *paths) {
    char path[CS_PATH_MAX];

    if (!paths) {
        return -1;
    }
    if (cs_daemon_state_make_path(paths, path, sizeof(path)) != 0) {
        return -1;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return -1;
    }

    return 0;
}

int cs_daemon_state_is_pid_running(pid_t pid) {
    if (pid <= 0) {
        return 0;
    }
    if (kill(pid, 0) == 0) {
        return 1;
    }

    return errno == EPERM ? 1 : 0;
}

int cs_daemon_wait_for_pid_exit(pid_t pid, int timeout_ms) {
    int waited = 0;

    if (pid <= 0 || timeout_ms < 0) {
        return -1;
    }

    for (;;) {
        pid_t wait_rc = waitpid(pid, NULL, WNOHANG);

        if (wait_rc == pid) {
            return 0;
        }
        if (wait_rc < 0 && errno != ECHILD) {
            return -1;
        }
        if (!cs_daemon_state_is_pid_running(pid)) {
            return 0;
        }
        if (waited >= timeout_ms) {
            return -1;
        }
        usleep(50 * 1000);
        waited += 50;
    }
}

int cs_daemon_wait_for_port_available(int port, int timeout_ms) {
    int waited = 0;

    if (port < 1 || port > 65535 || timeout_ms < 0) {
        return -1;
    }

    for (;;) {
        int can_bind = cs_daemon_can_bind_port(port);

        if (can_bind == 1) {
            return 0;
        }
        if (can_bind < 0) {
            return -1;
        }
        if (waited >= timeout_ms) {
            return -1;
        }

        usleep(50 * 1000);
        waited += 50;
    }
}

int cs_daemon_prepare_foreground_start(const cs_paths *paths,
                                       int requested_port,
                                       int port_explicitly_set,
                                       int *port_out) {
    cs_daemon_state state;
    int load_rc;
    int daemon_port = requested_port;

    if (!paths || !port_out || requested_port < 1 || requested_port > 65535) {
        return -1;
    }

    *port_out = requested_port;
    load_rc = cs_daemon_state_load(paths, &state);
    if (load_rc != 0) {
        (void) cs_daemon_state_clear(paths);
        if (cs_keep_awake_disable(paths) != 0) {
            return -1;
        }
        return 0;
    }
    if (state.port > 0) {
        daemon_port = state.port;
        if (!port_explicitly_set) {
            *port_out = state.port;
        }
    }
    if (state.pid <= 0) {
        (void) cs_daemon_state_clear(paths);
        if (cs_keep_awake_disable(paths) != 0) {
            return -1;
        }
        return 0;
    }

    if (!cs_daemon_state_is_pid_running(state.pid)) {
        (void) cs_daemon_state_clear(paths);
        if (cs_keep_awake_disable(paths) != 0) {
            return -1;
        }
        return 0;
    }

    if (kill(state.pid, SIGTERM) != 0 && errno != ESRCH) {
        return -1;
    }
    if (cs_daemon_wait_for_pid_exit(state.pid, 5000) != 0) {
        if (kill(state.pid, SIGKILL) != 0 && errno != ESRCH) {
            return -1;
        }
        if (cs_daemon_wait_for_pid_exit(state.pid, 2000) != 0) {
            return -1;
        }
    }
    if (cs_daemon_state_clear(paths) != 0) {
        return -1;
    }
    if (cs_keep_awake_disable(paths) != 0) {
        return -1;
    }
    if (cs_daemon_wait_for_port_available(daemon_port, 2000) != 0) {
        return -1;
    }

    return 0;
}
