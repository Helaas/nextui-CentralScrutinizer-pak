#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cs_session.h"

static int is_hex_string(const char *value) {
    size_t i;

    if (!value) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; ++i) {
        if (!isxdigit((unsigned char) value[i])) {
            return 0;
        }
    }

    return 1;
}

static void test_partial_fd_reads_are_completed(void) {
    int pipefd[2];
    pid_t child;
    unsigned char buffer[8];
    int status = 0;

    assert(pipe(pipefd) == 0);

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(pipefd[0]);
        assert(write(pipefd[1], "ABCD", 4) == 4);
        usleep(50 * 1000);
        assert(write(pipefd[1], "EFGH", 4) == 4);
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    assert(cs_session_fill_random_bytes_from_fd_for_test(pipefd[0], buffer, sizeof(buffer)) == 0);
    close(pipefd[0]);
    assert(memcmp(buffer, "ABCDEFGH", sizeof(buffer)) == 0);
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

int main(void) {
    char buffer[256];
    char same_token[256];
    char other_token[256];
    char tiny[4];
    char oversized[CS_SESSION_TOKEN_MAX_LEN + 2];
    size_t i;

    const char *disallowed_tokens[] = {
        " ",
        "bad token",
        "bad;token",
        "bad,token",
        "bad\"token",
        "bad\\token",
        "bad\r\ntoken",
        "bad\ttoken",
        "bad/token",
        "bad:token",
        "bad=token",
        "bad+token",
        "bad?token",
        "bad#token",
        "bad%token",
        "bad<token",
    };

    assert(cs_session_make_cookie(buffer, sizeof(buffer), "token-A.1_z") == 0);
    assert(strcmp(buffer,
                  "cs_trust=token-A.1_z; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000") == 0);
    assert(cs_session_make_csrf(buffer, sizeof(buffer), "token-A.1_z") == 0);
    assert(strlen(buffer) == CS_SESSION_CSRF_TOKEN_HEX_LEN);
    assert(strstr(buffer, "token-A.1_z") == NULL);
    assert(is_hex_string(buffer));
    assert(cs_session_make_csrf(same_token, sizeof(same_token), "token-A.1_z") == 0);
    assert(strcmp(buffer, same_token) == 0);
    assert(cs_session_make_csrf(other_token, sizeof(other_token), "token-B.1_z") == 0);
    assert(strcmp(buffer, other_token) != 0);

    assert(cs_session_make_cookie(buffer, sizeof(buffer),
                                  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.")
           == 0);
    assert(cs_session_make_csrf(buffer, sizeof(buffer),
                                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.")
           == 0);

    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    assert(cs_session_make_cookie(buffer, sizeof(buffer), oversized) == -1);
    assert(cs_session_make_csrf(buffer, sizeof(buffer), oversized) == -1);

    assert(cs_session_make_cookie(NULL, sizeof(buffer), "token") == -1);
    assert(cs_session_make_csrf(NULL, sizeof(buffer), "token") == -1);
    assert(cs_session_make_cookie(buffer, 0, "token") == -1);
    assert(cs_session_make_csrf(buffer, 0, "token") == -1);

    assert(cs_session_make_cookie(buffer, sizeof(buffer), NULL) == -1);
    assert(cs_session_make_csrf(buffer, sizeof(buffer), NULL) == -1);
    assert(cs_session_make_cookie(buffer, sizeof(buffer), "") == -1);
    assert(cs_session_make_csrf(buffer, sizeof(buffer), "") == -1);

    for (i = 0; i < sizeof(disallowed_tokens) / sizeof(disallowed_tokens[0]); ++i) {
        assert(cs_session_make_cookie(buffer, sizeof(buffer), disallowed_tokens[i]) == -1);
        assert(cs_session_make_csrf(buffer, sizeof(buffer), disallowed_tokens[i]) == -1);
    }

    assert(cs_session_make_cookie(tiny, sizeof(tiny), "token") == -1);
    assert(cs_session_make_csrf(tiny, sizeof(tiny), "token") == -1);

    assert(cs_session_make_csrf(buffer, CS_SESSION_CSRF_TOKEN_HEX_LEN, "token") == -1);
    assert(cs_session_make_csrf(buffer, CS_SESSION_CSRF_TOKEN_HEX_LEN + 1, "token") == 0);
    assert(strlen(buffer) == CS_SESSION_CSRF_TOKEN_HEX_LEN);
    assert(is_hex_string(buffer));

    test_partial_fd_reads_are_completed();

    return 0;
}
