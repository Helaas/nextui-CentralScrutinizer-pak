#ifndef CS_DAEMON_H
#define CS_DAEMON_H

#include <stddef.h>
#include <sys/types.h>

#include "cs_paths.h"

typedef struct cs_daemon_state {
    pid_t pid;
    int port;
} cs_daemon_state;

int cs_daemon_state_make_path(const cs_paths *paths, char *buffer, size_t buffer_len);
int cs_daemon_state_load(const cs_paths *paths, cs_daemon_state *state);
int cs_daemon_state_save(const cs_paths *paths, const cs_daemon_state *state);
int cs_daemon_state_clear(const cs_paths *paths);
int cs_daemon_state_is_pid_running(pid_t pid);
int cs_daemon_wait_for_pid_exit(pid_t pid, int timeout_ms);
int cs_daemon_wait_for_port_available(int port, int timeout_ms);
int cs_daemon_stay_awake_enable(void);
int cs_daemon_stay_awake_disable(void);
int cs_daemon_prepare_foreground_start(const cs_paths *paths,
                                       int requested_port,
                                       int port_explicitly_set,
                                       int *port_out);

#endif
