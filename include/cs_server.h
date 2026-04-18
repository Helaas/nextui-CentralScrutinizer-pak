#ifndef CS_SERVER_H
#define CS_SERVER_H

#include <stddef.h>

#define CS_SERVER_QR_PAIR_PATH "/pair/qr"
#define CS_SERVER_SECURITY_HEADERS_HTTP \
    "X-Content-Type-Options: nosniff\r\n" \
    "X-Frame-Options: DENY\r\n" \
    "Referrer-Policy: no-referrer\r\n" \
    "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' ws: wss:; font-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'\r\n"

struct cs_app;
struct mg_connection;

int cs_server_start(struct cs_app *app);
void cs_server_stop(void);
int cs_server_get_paired(void);
int cs_server_get_trusted_count(void);
int cs_server_reset_session(void);
int cs_server_revoke_browser(const char *cookie_header);
int cs_server_copy_pairing_code(char *buffer, size_t buffer_len);
int cs_server_consume_pairing_code(const char *code, int *throttled_out);
int cs_server_issue_qr_pair_token(char *buffer, size_t buffer_len);
int cs_server_consume_qr_pair_token(const char *token, int *expired_out);
int cs_server_qr_pair_token_is_active(const char *token);
int cs_server_get_qr_pair_token_ttl_seconds(void);
int cs_server_make_session_cookie(char *buffer, size_t buffer_len, const char *trust_token);
int cs_server_make_csrf_token(char *buffer, size_t buffer_len, const char *trust_token);
int cs_server_trust_browser(const char *browser_id, char *trust_token, size_t trust_token_len);
int cs_server_make_clear_cookie(char *buffer, size_t buffer_len);
int cs_server_copy_cookie_token(const char *cookie_header, char *buffer, size_t buffer_len);
int cs_server_cookie_is_valid(const char *cookie_header);
int cs_server_csrf_is_valid(const char *cookie_header, const char *csrf_header);
int cs_server_request_csrf_is_valid(const struct mg_connection *conn, int allow_query_param);
const char *cs_server_security_headers(void);

#endif
