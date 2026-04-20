#include <assert.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
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

#include "cs_app.h"
#include "cs_auth.h"
#include "cs_daemon.h"
#include "cs_keep_awake.h"
#include "cs_paths.h"
#include "cs_settings.h"

static int reserve_local_port(void) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
    assert(getsockname(fd, (struct sockaddr *) &addr, &addr_len) == 0);
    close(fd);
    return (int) ntohs(addr.sin_port);
}

static void make_dir(const char *path) {
    assert(mkdir(path, 0775) == 0 || errno == EEXIST);
}

static void make_dir_p(const char *path) {
    char buffer[CS_PATH_MAX];
    size_t i;

    assert(path != NULL);
    assert(strlen(path) < sizeof(buffer));
    snprintf(buffer, sizeof(buffer), "%s", path);

    for (i = 1; buffer[i] != '\0'; ++i) {
        if (buffer[i] != '/') {
            continue;
        }
        buffer[i] = '\0';
        make_dir(buffer);
        buffer[i] = '/';
    }

    make_dir(buffer);
}

static void remove_tree(const char *path) {
    DIR *dir;
    struct dirent *entry;

    assert(path != NULL);

    dir = opendir(path);
    assert(dir != NULL);

    while ((entry = readdir(dir)) != NULL) {
        char child_path[CS_PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        assert(snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name) < (int) sizeof(child_path));
        assert(lstat(child_path, &st) == 0);
        if (S_ISDIR(st.st_mode)) {
            remove_tree(child_path);
            continue;
        }
        assert(unlink(child_path) == 0);
    }

    assert(closedir(dir) == 0);
    assert(rmdir(path) == 0);
}

static pid_t spawn_sigterm_ignoring_listener(int port) {
    pid_t pid = fork();

    assert(pid >= 0);
    if (pid == 0) {
        struct sigaction sa;
        struct sockaddr_in addr;
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        (void) sigaction(SIGTERM, &sa, NULL);

        if (fd < 0) {
            _exit(1);
        }
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t) port);
        if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
            _exit(1);
        }
        for (;;) {
            pause();
        }
    }

    return pid;
}

static void wait_for_listener(int port, int timeout_ms) {
    int waited = 0;

    while (waited <= timeout_ms) {
        struct sockaddr_in addr;
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        assert(fd >= 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t) port);
        assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
        if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0) {
            close(fd);
            return;
        }
        close(fd);
        usleep(50 * 1000);
        waited += 50;
    }

    assert(!"listener did not become ready");
}

static void wait_for_daemon_state(const cs_paths *paths, cs_daemon_state *state, int timeout_ms) {
    int waited = 0;

    assert(paths != NULL);
    assert(state != NULL);

    while (waited <= timeout_ms) {
        if (cs_daemon_state_load(paths, state) == 0 && state->pid > 0) {
            return;
        }
        usleep(50 * 1000);
        waited += 50;
    }

    assert(!"daemon state was not persisted");
}

static void write_trust_store(const cs_paths *paths) {
    cs_trust_store store = {0};
    char trust_store_path[CS_PATH_MAX];

    assert(paths != NULL);
    make_dir_p(paths->shared_state_root);
    assert(snprintf(trust_store_path,
                    sizeof(trust_store_path),
                    "%s/trusted-clients.json",
                    paths->shared_state_root)
           < (int) sizeof(trust_store_path));
    assert(cs_trust_store_add(&store, "daemon-browser", "daemon-token") == 0);
    assert(cs_trust_store_save(trust_store_path, &store) == 0);
}

static void write_settings(const cs_paths *paths, int keep_awake_in_background) {
    cs_settings settings = {0};

    assert(paths != NULL);
    settings.terminal_enabled = 0;
    settings.keep_awake_in_background = keep_awake_in_background ? 1 : 0;
    assert(cs_settings_save(paths, &settings) == 0);
}

static void make_minui_settings_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    assert(paths != NULL);
    assert(buffer != NULL);
    assert(snprintf(buffer, buffer_len, "%s/minuisettings.txt", paths->shared_userdata_root) < (int) buffer_len);
}

static void make_keep_awake_state_path(const cs_paths *paths, char *buffer, size_t buffer_len) {
    assert(paths != NULL);
    assert(buffer != NULL);
    assert(snprintf(buffer, buffer_len, "%s/nextui-keep-awake-state.txt", paths->shared_state_root) < (int) buffer_len);
}

static void write_text_file(const char *path, const char *contents) {
    FILE *fp;
    char parent[CS_PATH_MAX];
    size_t i;

    assert(path != NULL);
    assert(contents != NULL);
    assert(strlen(path) < sizeof(parent));
    snprintf(parent, sizeof(parent), "%s", path);

    for (i = strlen(parent); i > 0; --i) {
        if (parent[i - 1] == '/') {
            parent[i - 1] = '\0';
            break;
        }
    }
    assert(i > 0);
    make_dir_p(parent);

    fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fwrite(contents, 1, strlen(contents), fp) == strlen(contents));
    assert(fclose(fp) == 0);
}

static char *read_text_file(const char *path) {
    FILE *fp;
    long size;
    char *contents;

    assert(path != NULL);
    fp = fopen(path, "rb");
    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    size = ftell(fp);
    assert(size >= 0);
    assert(fseek(fp, 0, SEEK_SET) == 0);
    contents = (char *) malloc((size_t) size + 1u);
    assert(contents != NULL);
    assert(size == 0 || fread(contents, 1, (size_t) size, fp) == (size_t) size);
    contents[size] = '\0';
    assert(fclose(fp) == 0);
    return contents;
}

static void assert_file_equals(const char *path, const char *expected) {
    char *contents;

    assert(path != NULL);
    assert(expected != NULL);
    contents = read_text_file(path);
    assert(strcmp(contents, expected) == 0);
    free(contents);
}

static void assert_file_contains(const char *path, const char *needle) {
    char *contents;

    assert(path != NULL);
    assert(needle != NULL);
    contents = read_text_file(path);
    assert(strstr(contents, needle) != NULL);
    free(contents);
}

static void run_app_child(const char *argv0,
                          const char *web_root,
                          const char *sdcard_root,
                          int port,
                          int *status_out) {
    char port_arg[16];
    char *argv[] = {(char *) argv0,
                    "--headless",
                    "--daemonized",
                    "--port",
                    port_arg,
                    "--web-root",
                    (char *) web_root,
                    "--sdcard",
                    (char *) sdcard_root,
                    NULL};
    pid_t pid;
    int status = 0;

    assert(status_out != NULL);
    assert(snprintf(port_arg, sizeof(port_arg), "%d", port) < (int) sizeof(port_arg));

    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        _exit(cs_app_run((int) (sizeof(argv) / sizeof(argv[0])) - 1, argv));
    }

    assert(waitpid(pid, &status, 0) == pid);
    *status_out = status;
}

int main(void) {
    char template[] = "/tmp/cs-daemon-XXXXXX";
    char web_root[CS_PATH_MAX];
    char daemon_state_path[CS_PATH_MAX];
    char minui_settings_path[CS_PATH_MAX];
    char keep_awake_state_path[CS_PATH_MAX];
    char argv0[] = "test-daemon";
    char port_arg[16];
    char *sdcard_root;
    cs_paths paths;
    cs_daemon_state state;
    pid_t stale_pid;
    pid_t launcher_pid;
    int port;
    int status = 0;
    int next_port = 0;

    sdcard_root = mkdtemp(template);
    assert(sdcard_root != NULL);
    assert(snprintf(web_root, sizeof(web_root), "%s/web", sdcard_root) < (int) sizeof(web_root));
    make_dir(web_root);

    assert(setenv("SDCARD_PATH", sdcard_root, 1) == 0);
    assert(setenv("CS_WEB_ROOT", web_root, 1) == 0);
    assert(setenv("CS_PLATFORM_NAME_OVERRIDE", "my355", 1) == 0);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_daemon_state_make_path(&paths, daemon_state_path, sizeof(daemon_state_path)) == 0);
    make_minui_settings_path(&paths, minui_settings_path, sizeof(minui_settings_path));
    make_keep_awake_state_path(&paths, keep_awake_state_path, sizeof(keep_awake_state_path));
    write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(cs_keep_awake_disable(&paths) == 0);

    assert(cs_daemon_state_is_pid_running(getpid()) == 1);

    stale_pid = fork();
    assert(stale_pid >= 0);
    if (stale_pid == 0) {
        _exit(0);
    }
    assert(waitpid(stale_pid, NULL, 0) == stale_pid);

    state.pid = stale_pid;
    state.port = 8877;
    write_settings(&paths, 1);
    assert(cs_daemon_state_save(&paths, &state) == 0);
    assert(cs_keep_awake_enable(&paths) == 0);
    next_port = 8877;
    assert(cs_daemon_prepare_foreground_start(&paths, next_port, 0, &next_port) == 0);
    assert(access(daemon_state_path, F_OK) != 0);
    assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(access(keep_awake_state_path, F_OK) != 0);

    port = reserve_local_port();
    assert(snprintf(port_arg, sizeof(port_arg), "%d", port) < (int) sizeof(port_arg));
    write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");
    run_app_child(argv0, web_root, sdcard_root, port, &status);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) != 0);
    assert(access(daemon_state_path, F_OK) != 0);
    assert(cs_daemon_wait_for_port_available(port, 1000) == 0);
    assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(access(keep_awake_state_path, F_OK) != 0);

    write_trust_store(&paths);
    write_settings(&paths, 0);
    write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");

    launcher_pid = fork();
    assert(launcher_pid >= 0);
    if (launcher_pid == 0) {
        pid_t daemon_pid = fork();
        char *argv[] = {(char *) argv0,
                        "--headless",
                        "--daemonized",
                        "--port",
                        port_arg,
                        "--web-root",
                        web_root,
                        "--sdcard",
                        sdcard_root,
                        NULL};

        assert(daemon_pid >= 0);
        if (daemon_pid == 0) {
            _exit(cs_app_run((int) (sizeof(argv) / sizeof(argv[0])) - 1, argv));
        }
        _exit(0);
    }
    assert(waitpid(launcher_pid, &status, 0) == launcher_pid);

    wait_for_listener(port, 5000);
    wait_for_daemon_state(&paths, &state, 5000);
    assert(state.port == port);
    assert(cs_daemon_state_is_pid_running(state.pid) == 1);
    assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(access(keep_awake_state_path, F_OK) != 0);

    next_port = port;
    assert(cs_daemon_prepare_foreground_start(&paths, port, 1, &next_port) == 0);
    assert(cs_daemon_state_is_pid_running(state.pid) == 0);
    assert(access(daemon_state_path, F_OK) != 0);
    assert(cs_daemon_wait_for_port_available(port, 1000) == 0);
    assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(access(keep_awake_state_path, F_OK) != 0);

    write_settings(&paths, 1);
    write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");

    launcher_pid = fork();
    assert(launcher_pid >= 0);
    if (launcher_pid == 0) {
        pid_t daemon_pid = fork();
        char *argv[] = {(char *) argv0,
                        "--headless",
                        "--daemonized",
                        "--port",
                        port_arg,
                        "--web-root",
                        web_root,
                        "--sdcard",
                        sdcard_root,
                        NULL};

        assert(daemon_pid >= 0);
        if (daemon_pid == 0) {
            _exit(cs_app_run((int) (sizeof(argv) / sizeof(argv[0])) - 1, argv));
        }
        _exit(0);
    }
    assert(waitpid(launcher_pid, &status, 0) == launcher_pid);

    wait_for_listener(port, 5000);
    wait_for_daemon_state(&paths, &state, 5000);
    assert(state.port == port);
    assert(cs_daemon_state_is_pid_running(state.pid) == 1);
    assert_file_contains(minui_settings_path, "screentimeout=0\n");
    assert_file_contains(minui_settings_path, "centralscrutinizer_keepawake=1\n");
    assert(access(keep_awake_state_path, F_OK) == 0);

    next_port = port;
    assert(cs_daemon_prepare_foreground_start(&paths, port, 1, &next_port) == 0);
    assert(cs_daemon_state_is_pid_running(state.pid) == 0);
    assert(access(daemon_state_path, F_OK) != 0);
    assert(cs_daemon_wait_for_port_available(port, 1000) == 0);
    assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
    assert(access(keep_awake_state_path, F_OK) != 0);

    {
        pid_t stale_marker_pid = fork();

        assert(stale_marker_pid >= 0);
        if (stale_marker_pid == 0) {
            _exit(0);
        }
        assert(waitpid(stale_marker_pid, NULL, 0) == stale_marker_pid);

        state.pid = stale_marker_pid;
        state.port = port;
        write_settings(&paths, 1);
        write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");
        assert(cs_daemon_state_save(&paths, &state) == 0);
        assert(cs_keep_awake_enable(&paths) == 0);
        write_settings(&paths, 0);

        next_port = port;
        assert(cs_daemon_prepare_foreground_start(&paths, port, 1, &next_port) == 0);
        assert(access(daemon_state_path, F_OK) != 0);
        assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
        assert(access(keep_awake_state_path, F_OK) != 0);
    }

    {
        pid_t stubborn_pid = spawn_sigterm_ignoring_listener(port);
        cs_daemon_state stubborn_state = {.pid = stubborn_pid, .port = port};

        wait_for_listener(port, 5000);
        write_settings(&paths, 1);
        write_text_file(minui_settings_path, "volume=6\nscreentimeout=60\n");
        assert(cs_daemon_state_save(&paths, &stubborn_state) == 0);
        assert(cs_keep_awake_enable(&paths) == 0);

        assert(cs_daemon_prepare_foreground_start(&paths, port, 1, &next_port) == 0);
        assert(cs_daemon_state_is_pid_running(stubborn_pid) == 0);
        assert(access(daemon_state_path, F_OK) != 0);
        assert(cs_daemon_wait_for_port_available(port, 1000) == 0);
        assert_file_equals(minui_settings_path, "volume=6\nscreentimeout=60\n");
        assert(access(keep_awake_state_path, F_OK) != 0);
        (void) waitpid(stubborn_pid, NULL, WNOHANG);
    }

    assert(cs_keep_awake_disable(&paths) == 0);
    remove_tree(sdcard_root);
    return 0;
}
