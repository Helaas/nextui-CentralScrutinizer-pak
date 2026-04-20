#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_auth.h"
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

int main(void) {
    cs_pairing pairing = {0};
    cs_trust_store store = {0};
    cs_trust_store reloaded = {0};
    cs_trust_store missing = {0};
    char path_template[] = "/tmp/cs-auth-XXXXXX.json";
    char legacy_store_template[] = "/tmp/cs-auth-legacy-XXXXXX.json";
    char empty_store_template[] = "/tmp/cs-auth-empty-XXXXXX.json";
    char bad_file_template[] = "/tmp/cs-auth-bad-XXXXXX.json";
    char bad_dir_template[] = "/tmp/cs-auth-dir-XXXXXX";
    char malformed_json_template[] = "/tmp/cs-auth-malformed-XXXXXX.json";
    char malformed_entry_template[] = "/tmp/cs-auth-entry-XXXXXX.json";
    char missing_clients_template[] = "/tmp/cs-auth-schema-XXXXXX.json";
    char nested_clients_template[] = "/tmp/cs-auth-nested-XXXXXX.json";
    char trailing_content_template[] = "/tmp/cs-auth-trailing-XXXXXX.json";
    char unknown_key_template[] = "/tmp/cs-auth-unknown-XXXXXX.json";
    char duplicate_key_template[] = "/tmp/cs-auth-duplicate-XXXXXX.json";
    char cookie[256];
    char csrf[256];
    char csrf_again[256];
    int fd;
    int empty_fd;
    int bad_fd;
    int legacy_fd;
    int malformed_fd;
    size_t i;
    char long_browser_id[160];
    char legacy_json[512];
    cs_trust_store full_store = {0};
    cs_trust_store expiring_store = {0};

    assert(cs_pairing_generate(&pairing) == 0);
    assert(strlen(pairing.code) == 4);
    assert(isdigit((unsigned char) pairing.code[0]));
    assert(isdigit((unsigned char) pairing.code[1]));
    assert(isdigit((unsigned char) pairing.code[2]));
    assert(isdigit((unsigned char) pairing.code[3]));

    empty_fd = mkstemps(empty_store_template, 5);
    assert(empty_fd >= 0);
    assert(write(empty_fd, "{\"clients\":[]}", 14) == 14);
    close(empty_fd);
    assert(cs_trust_store_load(empty_store_template, &store) == 0);
    assert(remove(empty_store_template) == 0);
    assert(store.count == 0);
    assert(cs_trust_store_add(&store, "desktop-chrome", "plain-token") == 0);
    assert(store.count == 1);
    assert(strcmp(store.items[0].token_hash, "plain-token") != 0);
    assert(store.items[0].last_seen_at > 0);
    assert(cs_trust_store_has_token(&store, "plain-token", store.items[0].expires_at - 1, 0) == 1);
    assert(cs_trust_store_has_token(&store, "plain-token", store.items[0].expires_at + 1, 0) == 0);
    assert(cs_trust_store_add(&store, "desktop-chrome", "rotated-token") == 0);
    assert(store.count == 1);
    assert(cs_trust_store_has_token(&store, "plain-token", store.items[0].expires_at - 1, 0) == 0);
    assert(cs_trust_store_has_token(&store, "rotated-token", store.items[0].expires_at - 1, 0) == 1);
    assert(cs_trust_store_touch_token(&store,
                                      "rotated-token",
                                      store.items[0].last_seen_at + 30,
                                      CS_SESSION_IDLE_TIMEOUT_SECONDS,
                                      60)
           == 2);
    assert(cs_trust_store_touch_token(&store,
                                      "rotated-token",
                                      store.items[0].last_seen_at + 61,
                                      CS_SESSION_IDLE_TIMEOUT_SECONDS,
                                      60)
           == 1);
    assert(store.items[0].last_seen_at > 61);
    assert(cs_trust_store_add(&store, "desktop-firefox", "firefox-token") == 0);
    assert(store.count == 2);
    assert(cs_trust_store_remove_token(&store, "missing-token") == 0);
    assert(cs_trust_store_remove_token(&store, "firefox-token") == 1);
    assert(store.count == 1);
    assert(strcmp(store.items[0].browser_id, "desktop-chrome") == 0);
    assert(cs_trust_store_has_token(&store, "firefox-token", store.items[0].expires_at - 1, 0) == 0);
    assert(cs_trust_store_has_token(&store, "rotated-token", store.items[0].expires_at - 1, 0) == 1);

    assert(cs_trust_store_load("/tmp/cs-auth-missing.json", &missing) == 0);
    assert(missing.count == 0);

    malformed_fd = mkstemps(missing_clients_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd, "{\"not_clients\":[]}", 18) == 18);
    close(malformed_fd);
    assert(cs_trust_store_load(missing_clients_template, &missing) == -1);
    assert(remove(missing_clients_template) == 0);

    malformed_fd = mkstemps(nested_clients_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd, "{\"meta\":{\"clients\":[]},\"other\":[]}", 34) == 34);
    close(malformed_fd);
    assert(cs_trust_store_load(nested_clients_template, &missing) == -1);
    assert(remove(nested_clients_template) == 0);

    malformed_fd = mkstemps(trailing_content_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd, "{\"clients\":[]}[]", 16) == 16);
    close(malformed_fd);
    assert(cs_trust_store_load(trailing_content_template, &missing) == -1);
    assert(remove(trailing_content_template) == 0);

    malformed_fd = mkstemps(malformed_json_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd, "{\"clients\":[", 12) == 12);
    close(malformed_fd);
    assert(cs_trust_store_load(malformed_json_template, &missing) == -1);
    assert(remove(malformed_json_template) == 0);

    malformed_fd = mkstemps(malformed_entry_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd,
                 "{\"clients\":[{\"browser_id\":\"desktop-chrome\",\"token_hash\":\"nothex\",\"expires_at\":1}]}",
                 82) == 82);
    close(malformed_fd);
    assert(cs_trust_store_load(malformed_entry_template, &missing) == -1);
    assert(remove(malformed_entry_template) == 0);

    malformed_fd = mkstemps(unknown_key_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd,
                 "{\"clients\":[{\"browser_id\":\"desktop-chrome\",\"token_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"expires_at\":1,\"extra\":true}]}",
                 153) == 153);
    close(malformed_fd);
    assert(cs_trust_store_load(unknown_key_template, &missing) == -1);
    assert(remove(unknown_key_template) == 0);

    malformed_fd = mkstemps(duplicate_key_template, 5);
    assert(malformed_fd >= 0);
    assert(write(malformed_fd,
                 "{\"clients\":[{\"browser_id\":\"desktop-chrome\",\"browser_id\":\"desktop-chrome\",\"token_hash\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"expires_at\":1}]}",
                 170) == 170);
    close(malformed_fd);
    assert(cs_trust_store_load(duplicate_key_template, &missing) == -1);
    assert(remove(duplicate_key_template) == 0);

    bad_fd = mkstemps(bad_file_template, 5);
    assert(bad_fd >= 0);
    close(bad_fd);
    assert(chmod(bad_file_template, 0000) == 0);
    assert(cs_trust_store_load(bad_file_template, &missing) == -1);
    assert(chmod(bad_file_template, 0600) == 0);
    assert(remove(bad_file_template) == 0);

    assert(mkdtemp(bad_dir_template) != NULL);
    assert(cs_trust_store_load(bad_dir_template, &missing) == -1);
    assert(rmdir(bad_dir_template) == 0);

    assert(cs_trust_store_add(&store, "desktop\"chrome", "plain-token") == -1);
    memset(long_browser_id, 'a', sizeof(long_browser_id) - 1);
    long_browser_id[sizeof(long_browser_id) - 1] = '\0';
    assert(cs_trust_store_add(&store, long_browser_id, "plain-token") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad token") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad;token") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad\r\ntoken") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad,token") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad\"token") == -1);
    assert(cs_trust_store_add(&store, "desktop-chrome", "bad\\token") == -1);

    assert(cs_session_make_cookie(cookie, sizeof(cookie), "plain-token") == 0);
    assert(strcmp(cookie, "cs_trust=plain-token; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000") == 0);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "plain-token") == 0);
    assert(strlen(csrf) == CS_SESSION_CSRF_TOKEN_HEX_LEN);
    assert(strstr(csrf, "plain-token") == NULL);
    assert(is_hex_string(csrf));
    assert(cs_session_make_csrf(csrf_again, sizeof(csrf_again), "plain-token") == 0);
    assert(strcmp(csrf, csrf_again) == 0);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), NULL) == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), NULL) == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad;token") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad;token") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad\r\ntoken") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad\r\ntoken") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad token") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad token") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad,token") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad,token") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad\"token") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad\"token") == -1);
    assert(cs_session_make_cookie(cookie, sizeof(cookie), "bad\\token") == -1);
    assert(cs_session_make_csrf(csrf, sizeof(csrf), "bad\\token") == -1);

    assert(cs_trust_store_add(&expiring_store, "desktop-chrome", "rotated-token") == 0);
    assert(cs_trust_store_add(&expiring_store, "browser-a", "token-a") == 0);
    assert(cs_trust_store_add(&expiring_store, "browser-b", "token-b") == 0);
    expiring_store.items[0].expires_at = 100;
    expiring_store.items[1].expires_at = 50;
    expiring_store.items[2].expires_at = 300;
    assert(cs_trust_store_remove_expired(&expiring_store, 150) == 2);
    assert(expiring_store.count == 1);
    assert(strcmp(expiring_store.items[0].browser_id, "browser-b") == 0);
    assert(cs_trust_store_has_token(&expiring_store, "token-b", 200, CS_SESSION_IDLE_TIMEOUT_SECONDS) == 1);
    expiring_store.items[0].last_seen_at = 10;
    assert(cs_trust_store_has_token(&expiring_store, "token-b", 200, 60) == 0);

    fd = mkstemps(path_template, 5);
    assert(fd >= 0);
    close(fd);

    assert(cs_trust_store_save(path_template, &store) == 0);
    assert(cs_trust_store_load(path_template, &reloaded) == 0);
    assert(reloaded.count == 1);
    assert(strcmp(reloaded.items[0].browser_id, "desktop-chrome") == 0);
    assert(strcmp(reloaded.items[0].token_hash, "plain-token") != 0);
    assert(strcmp(reloaded.items[0].token_hash, store.items[0].token_hash) == 0);
    assert(reloaded.items[0].expires_at == store.items[0].expires_at);
    assert(reloaded.items[0].last_seen_at == store.items[0].last_seen_at);

    legacy_fd = mkstemps(legacy_store_template, 5);
    assert(legacy_fd >= 0);
    assert(snprintf(legacy_json,
                    sizeof(legacy_json),
                    "{\"clients\":[{\"browser_id\":\"desktop-chrome\",\"token_hash\":\"%s\",\"expires_at\":%lld}]}",
                    store.items[0].token_hash,
                    store.items[0].expires_at)
           > 0);
    assert(write(legacy_fd, legacy_json, strlen(legacy_json)) == (ssize_t) strlen(legacy_json));
    close(legacy_fd);
    assert(cs_trust_store_load(legacy_store_template, &reloaded) == 0);
    assert(reloaded.count == 1);
    assert(reloaded.items[0].last_seen_at > 0);
    assert(remove(legacy_store_template) == 0);

    strcpy(store.items[0].token_hash, "not-a-valid-hash");
    assert(cs_trust_store_save(path_template, &store) == -1);
    assert(cs_trust_store_load(path_template, &reloaded) == 0);
    assert(reloaded.count == 1);
    assert(strcmp(reloaded.items[0].browser_id, "desktop-chrome") == 0);

    for (i = 0; i < CS_TRUST_STORE_CAPACITY; ++i) {
        char browser_id[32];
        char token_value[32];

        assert(snprintf(browser_id, sizeof(browser_id), "browser-%zu", i) > 0);
        assert(snprintf(token_value, sizeof(token_value), "token-%zu", i) > 0);
        assert(cs_trust_store_add(&full_store, browser_id, token_value) == 0);
        full_store.items[i].expires_at = (long long) i + 1;
    }
    assert(full_store.count == CS_TRUST_STORE_CAPACITY);
    assert(cs_trust_store_add(&full_store, "extra-browser", "extra-token") == 0);
    assert(full_store.count == CS_TRUST_STORE_CAPACITY);
    assert(strcmp(full_store.items[0].browser_id, "extra-browser") == 0);

    store.count = CS_TRUST_STORE_CAPACITY + 1;
    assert(cs_trust_store_save(path_template, &store) == -1);
    assert(cs_trust_store_load(path_template, &reloaded) == 0);
    assert(reloaded.count == 1);
    assert(strcmp(reloaded.items[0].browser_id, "desktop-chrome") == 0);

    assert(remove(path_template) == 0);

    return 0;
}
