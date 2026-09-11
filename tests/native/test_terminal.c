#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

int cs_terminal_write_input_for_test(int fd, const char *data, size_t len);

static void assert_input(const char *input, const char *expected) {
    int fds[2];
    char output[128];
    size_t expected_len = strlen(expected);

    assert(pipe(fds) == 0);
    assert(cs_terminal_write_input_for_test(fds[1], input, strlen(input)) == 0);
    assert(close(fds[1]) == 0);
    assert(read(fds[0], output, sizeof(output)) == (ssize_t) expected_len);
    assert(memcmp(output, expected, expected_len) == 0);
    assert(close(fds[0]) == 0);
}

int main(void) {
    /* xterm replies to BusyBox's cursor query before the next typed command. */
    assert_input("\x1b[5;15Rls\r", "\x1b[5;15Rls\r");
    assert_input("\x1b[10;15Rpwd\r", "\x1b[10;15Rpwd\r");
    assert_input("\x1b[?10;15R", "\x1b[?10;15R");
    assert_input("\x1b[A\x1b[D\x1b[3~\003", "\x1b[A\x1b[D\x1b[3~\003");
    assert_input("ls\001\r", "ls\r");
    return 0;
}
