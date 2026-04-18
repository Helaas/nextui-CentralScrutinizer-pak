#ifndef CS_SESSION_H
#define CS_SESSION_H

#include <stddef.h>

#define CS_SESSION_COOKIE_MAX_AGE_SECONDS 2592000
#define CS_SESSION_IDLE_TIMEOUT_SECONDS 86400
#define CS_SESSION_CSRF_TOKEN_HEX_LEN 64

int cs_session_init_csrf_secret(void);
int cs_session_make_cookie(char *buffer, size_t buffer_len, const char *token);
int cs_session_make_csrf(char *buffer, size_t buffer_len, const char *token);

#endif
