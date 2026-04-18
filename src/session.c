#include "cs_session.h"

#include "../third_party/sha256/sha256.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CS_SESSION_CSRF_SECRET_BYTES 32
#define CS_SESSION_SHA256_BLOCK_BYTES 64

static unsigned char g_csrf_secret[CS_SESSION_CSRF_SECRET_BYTES];
static int g_csrf_secret_ready = 0;
static pthread_once_t g_csrf_secret_once = PTHREAD_ONCE_INIT;

static int cs_session_random_bytes(void *buffer, size_t len) {
    int fd;
    ssize_t nread;

    if (!buffer || len == 0) {
        return -1;
    }

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(buffer, len);
    return 0;
#endif

#if defined(HAVE_GETENTROPY)
    if (getentropy(buffer, len) == 0) {
        return 0;
    }
#endif

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    nread = read(fd, buffer, len);
    close(fd);
    return nread == (ssize_t) len ? 0 : -1;
}

static void cs_session_init_csrf_secret_once(void) {
    g_csrf_secret_ready = cs_session_random_bytes(g_csrf_secret, sizeof(g_csrf_secret)) == 0 ? 1 : -1;
}

static int cs_session_ensure_csrf_secret(void) {
    if (pthread_once(&g_csrf_secret_once, cs_session_init_csrf_secret_once) != 0) {
        return -1;
    }

    return g_csrf_secret_ready == 1 ? 0 : -1;
}

static void cs_session_hmac_sha256(uint8_t hash[32], const char *token) {
    uint8_t key_block[CS_SESSION_SHA256_BLOCK_BYTES] = {0};
    uint8_t ipad[CS_SESSION_SHA256_BLOCK_BYTES];
    uint8_t opad[CS_SESSION_SHA256_BLOCK_BYTES];
    size_t token_len = strlen(token);
    uint8_t inner_input[CS_SESSION_SHA256_BLOCK_BYTES + token_len];
    uint8_t outer_input[CS_SESSION_SHA256_BLOCK_BYTES + 32];
    uint8_t inner_hash[32];
    size_t i;

    memcpy(key_block, g_csrf_secret, sizeof(g_csrf_secret));
    for (i = 0; i < CS_SESSION_SHA256_BLOCK_BYTES; ++i) {
        ipad[i] = (uint8_t) (key_block[i] ^ 0x36u);
        opad[i] = (uint8_t) (key_block[i] ^ 0x5cu);
    }

    memcpy(inner_input, ipad, sizeof(ipad));
    memcpy(inner_input + sizeof(ipad), token, token_len);
    calc_sha_256(inner_hash, inner_input, sizeof(inner_input));

    memcpy(outer_input, opad, sizeof(opad));
    memcpy(outer_input + sizeof(opad), inner_hash, sizeof(inner_hash));
    calc_sha_256(hash, outer_input, sizeof(outer_input));
}

static int cs_session_token_char_is_allowed(unsigned char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           ch == '-' || ch == '_' || ch == '.';
}

static int cs_session_token_is_safe(const char *token) {
    size_t i;

    if (!token || token[0] == '\0') {
        return 0;
    }

    for (i = 0; token[i] != '\0'; ++i) {
        if (!cs_session_token_char_is_allowed((unsigned char) token[i])) {
            return 0;
        }
    }

    return 1;
}

int cs_session_init_csrf_secret(void) {
    return cs_session_ensure_csrf_secret();
}

int cs_session_make_cookie(char *buffer, size_t buffer_len, const char *token) {
    if (!buffer || buffer_len == 0 || !cs_session_token_is_safe(token)) {
        return -1;
    }

    return snprintf(buffer,
                    buffer_len,
                    "cs_trust=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d",
                    token,
                    CS_SESSION_COOKIE_MAX_AGE_SECONDS) < (int) buffer_len
               ? 0
               : -1;
}

int cs_session_make_csrf(char *buffer, size_t buffer_len, const char *token) {
    uint8_t hash[32];
    size_t i;

    if (!buffer || buffer_len == 0 || !cs_session_token_is_safe(token)) {
        return -1;
    }
    if (buffer_len <= CS_SESSION_CSRF_TOKEN_HEX_LEN || cs_session_ensure_csrf_secret() != 0) {
        return -1;
    }

    cs_session_hmac_sha256(hash, token);
    for (i = 0; i < sizeof(hash); ++i) {
        if (snprintf(buffer + (i * 2), buffer_len - (i * 2), "%02x", hash[i]) != 2) {
            return -1;
        }
    }
    buffer[CS_SESSION_CSRF_TOKEN_HEX_LEN] = '\0';
    return 0;
}
