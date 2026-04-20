#include "cs_auth.h"
#include "cs_session.h"
#include "cs_util.h"

#include "../third_party/jsmn/jsmn.h"
#include "../third_party/sha256/sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cs_random_u32(uint32_t *value) {
    int fd;
    ssize_t nread;

    if (!value) {
        return -1;
    }

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(value, sizeof(*value));
    return 0;
#endif

#if defined(HAVE_GETENTROPY)
    if (getentropy(value, sizeof(*value)) == 0) {
        return 0;
    }
#endif

    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        nread = read(fd, value, sizeof(*value));
        close(fd);
        if (nread == (ssize_t) sizeof(*value)) {
            return 0;
        }
    }

    return -1;
}

int cs_const_time_memcmp(const void *left, const void *right, size_t len) {
    const unsigned char *lhs = (const unsigned char *) left;
    const unsigned char *rhs = (const unsigned char *) right;
    unsigned char diff = 0;
    size_t i;

    if ((!lhs || !rhs) && len > 0) {
        return -1;
    }

    for (i = 0; i < len; ++i) {
        diff |= (unsigned char) (lhs[i] ^ rhs[i]);
    }

    return (int) diff;
}

static int cs_browser_id_is_valid(const char *browser_id) {
    size_t i;

    if (!browser_id) {
        return 0;
    }

    if (browser_id[0] == '\0' || strlen(browser_id) >= sizeof(((cs_trust_item *) 0)->browser_id)) {
        return 0;
    }

    for (i = 0; browser_id[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) browser_id[i];

        if (!(isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
            return 0;
        }
    }

    return 1;
}

static int cs_token_hash_is_valid(const char *token_hash) {
    size_t i;

    if (!token_hash || strlen(token_hash) != 64) {
        return 0;
    }

    for (i = 0; token_hash[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) token_hash[i];

        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
            return 0;
        }
    }

    return 1;
}

static int cs_plain_token_is_valid(const char *token) {
    size_t i;

    if (!token || token[0] == '\0') {
        return 0;
    }

    for (i = 0; token[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) token[i];

        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              ch == '-' || ch == '_' || ch == '.')) {
            return 0;
        }
    }

    return 1;
}

int cs_pairing_generate(cs_pairing *pairing) {
    uint32_t random_value;
    unsigned int value;

    if (!pairing) {
        return -1;
    }

    if (cs_random_u32(&random_value) != 0) {
        return -1;
    }

    value = 1000u + (random_value % 9000u);
    return snprintf(pairing->code, sizeof(pairing->code), "%04d", value) == 4 ? 0 : -1;
}

static int cs_hash_token(char out[65], const char *plain_token) {
    uint8_t hash[32];
    int i;

    calc_sha_256(hash, plain_token, strlen(plain_token));
    for (i = 0; i < 32; ++i) {
        if (CS_SAFE_SNPRINTF(out + (i * 2), 65u - (size_t) (i * 2), "%02x", hash[i]) != 0) {
            return -1;
        }
    }
    out[64] = '\0';
    return 0;
}

static int cs_token_eq(const char *json, const jsmntok_t *token, const char *expected) {
    size_t expected_len = strlen(expected);
    size_t token_len = (size_t) (token->end - token->start);

    return token->type == JSMN_STRING &&
           token_len == expected_len &&
           strncmp(json + token->start, expected, token_len) == 0;
}

static int cs_copy_token_string(char *dest, size_t dest_len, const char *json, const jsmntok_t *token) {
    size_t token_len;

    if (!dest || !json || !token || token->type != JSMN_STRING || dest_len == 0) {
        return -1;
    }

    token_len = (size_t) (token->end - token->start);
    if (token_len >= dest_len) {
        return -1;
    }

    memcpy(dest, json + token->start, token_len);
    dest[token_len] = '\0';
    return 0;
}

static int cs_copy_token_long_long(long long *out, const char *json, const jsmntok_t *token) {
    char number[32];
    size_t token_len;
    char *endptr;
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
    value = strtoll(number, &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        return -1;
    }

    *out = value;
    return 0;
}

int cs_trust_store_load(const char *path, cs_trust_store *store) {
    FILE *fp;
    long file_size;
    char *json;
    jsmn_parser parser;
    jsmntok_t tokens[256];
    int token_count;
    size_t count = 0;
    int array_index;
    int entries;
    long long loaded_at;

    if (!path || !store) {
        return -1;
    }

    memset(store, 0, sizeof(*store));
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
    json[file_size] = '\0';
    fclose(fp);

    loaded_at = (long long) time(NULL);
    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, (size_t) file_size, tokens, sizeof(tokens) / sizeof(tokens[0]));
    if (token_count < 3 || tokens[0].type != JSMN_OBJECT) {
        free(json);
        return -1;
    }

    if (tokens[0].size != 1 || !cs_token_eq(json, &tokens[1], "clients")) {
        free(json);
        return -1;
    }

    if (tokens[2].type != JSMN_ARRAY) {
        free(json);
        return -1;
    }

    entries = tokens[2].size;
    array_index = 3;
    while (entries-- > 0) {
        int fields;
        cs_trust_item *item;
        int seen_browser_id = 0;
        int seen_token_hash = 0;
        int seen_expires_at = 0;
        int seen_last_seen_at = 0;

        if (count >= CS_TRUST_STORE_CAPACITY || array_index >= token_count || tokens[array_index].type != JSMN_OBJECT) {
            free(json);
            return -1;
        }

        item = &store->items[count];
        memset(item, 0, sizeof(*item));
        fields = tokens[array_index].size;
        array_index++;

        while (fields-- > 0) {
            if (array_index + 1 >= token_count) {
                free(json);
                return -1;
            }

            if (cs_token_eq(json, &tokens[array_index], "browser_id")) {
                if (seen_browser_id) {
                    free(json);
                    return -1;
                }
                if (cs_copy_token_string(item->browser_id,
                                         sizeof(item->browser_id),
                                         json,
                                         &tokens[array_index + 1]) != 0 ||
                    !cs_browser_id_is_valid(item->browser_id)) {
                    free(json);
                    return -1;
                }
                seen_browser_id = 1;
            } else if (cs_token_eq(json, &tokens[array_index], "token_hash")) {
                if (seen_token_hash) {
                    free(json);
                    return -1;
                }
                if (cs_copy_token_string(item->token_hash,
                                         sizeof(item->token_hash),
                                         json,
                                         &tokens[array_index + 1]) != 0 ||
                    !cs_token_hash_is_valid(item->token_hash)) {
                    free(json);
                    return -1;
                }
                seen_token_hash = 1;
            } else if (cs_token_eq(json, &tokens[array_index], "expires_at")) {
                if (seen_expires_at) {
                    free(json);
                    return -1;
                }
                if (cs_copy_token_long_long(&item->expires_at, json, &tokens[array_index + 1]) != 0) {
                    free(json);
                    return -1;
                }
                seen_expires_at = 1;
            } else if (cs_token_eq(json, &tokens[array_index], "last_seen_at")) {
                if (seen_last_seen_at) {
                    free(json);
                    return -1;
                }
                if (cs_copy_token_long_long(&item->last_seen_at, json, &tokens[array_index + 1]) != 0) {
                    free(json);
                    return -1;
                }
                seen_last_seen_at = 1;
            } else {
                free(json);
                return -1;
            }

            array_index += 2;
        }

        if (!seen_browser_id || !seen_token_hash || !seen_expires_at) {
            free(json);
            return -1;
        }
        if (!seen_last_seen_at) {
            item->last_seen_at = loaded_at;
        }

        count++;
    }

    if (array_index != token_count) {
        free(json);
        return -1;
    }

    store->count = count;
    free(json);
    return 0;
}

int cs_trust_store_add(cs_trust_store *store, const char *browser_id, const char *plain_token) {
    cs_trust_item *item;
    size_t target_index = 0;
    size_t i;
    long long now;

    if (!store || !cs_browser_id_is_valid(browser_id) || !cs_plain_token_is_valid(plain_token)) {
        return -1;
    }

    now = (long long) time(NULL);
    for (i = 0; i < store->count; ++i) {
        if (strcmp(store->items[i].browser_id, browser_id) == 0) {
            target_index = i;
            item = &store->items[target_index];
            if (snprintf(item->browser_id, sizeof(item->browser_id), "%s", browser_id) >= (int) sizeof(item->browser_id)) {
                return -1;
            }
            if (cs_hash_token(item->token_hash, plain_token) != 0) {
                return -1;
            }
            item->expires_at = now + CS_SESSION_COOKIE_MAX_AGE_SECONDS;
            item->last_seen_at = now;
            return 0;
        }
    }

    if (store->count < CS_TRUST_STORE_CAPACITY) {
        target_index = store->count++;
    } else {
        for (i = 1; i < store->count; ++i) {
            if (store->items[i].expires_at < store->items[target_index].expires_at) {
                target_index = i;
            }
        }
    }

    item = &store->items[target_index];
    if (snprintf(item->browser_id, sizeof(item->browser_id), "%s", browser_id) >= (int) sizeof(item->browser_id)) {
        return -1;
    }
    if (cs_hash_token(item->token_hash, plain_token) != 0) {
        return -1;
    }
    item->expires_at = now + CS_SESSION_COOKIE_MAX_AGE_SECONDS;
    item->last_seen_at = now;
    return 0;
}

int cs_trust_store_save(const char *path, const cs_trust_store *store) {
    FILE *fp = NULL;
    size_t i;
    char temp_path[PATH_MAX];
    int fd;
    int status = -1;

    if (!path || !store || store->count > CS_TRUST_STORE_CAPACITY || strlen(path) + sizeof(".tmpXXXXXX") > sizeof(temp_path)) {
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

    if (fputs("{\"clients\":[", fp) == EOF) {
        goto cleanup;
    }

    for (i = 0; i < store->count; ++i) {
        const cs_trust_item *item = &store->items[i];

        if (!cs_browser_id_is_valid(item->browser_id) || !cs_token_hash_is_valid(item->token_hash)) {
            goto cleanup;
        }

        if (i > 0) {
            if (fputc(',', fp) == EOF) {
                goto cleanup;
            }
        }

        if (fprintf(fp,
                    "{\"browser_id\":\"%s\",\"token_hash\":\"%s\",\"expires_at\":%lld,\"last_seen_at\":%lld}",
                    item->browser_id,
                    item->token_hash,
                    item->expires_at,
                    item->last_seen_at) < 0) {
            goto cleanup;
        }
    }

    if (fputs("]}", fp) == EOF) {
        goto cleanup;
    }

    if (fclose(fp) != 0) {
        fp = NULL;
        unlink(temp_path);
        return -1;
    }
    fp = NULL;

    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }

    return 0;

cleanup:
    if (fp) {
        fclose(fp);
    }
    unlink(temp_path);
    return status;
}

int cs_trust_store_remove_expired(cs_trust_store *store, long long now) {
    size_t read_index;
    size_t write_index = 0;
    int removed = 0;

    if (!store) {
        return -1;
    }

    for (read_index = 0; read_index < store->count; ++read_index) {
        if (store->items[read_index].expires_at <= now) {
            removed += 1;
            continue;
        }
        if (write_index != read_index) {
            store->items[write_index] = store->items[read_index];
        }
        write_index += 1;
    }

    while (write_index < store->count) {
        memset(&store->items[write_index], 0, sizeof(store->items[write_index]));
        write_index += 1;
    }

    store->count -= (size_t) removed;
    return removed;
}

int cs_trust_store_remove_token(cs_trust_store *store, const char *plain_token) {
    char token_hash[65];
    size_t i;

    if (!store || !cs_plain_token_is_valid(plain_token)) {
        return -1;
    }
    if (cs_hash_token(token_hash, plain_token) != 0) {
        return -1;
    }

    for (i = 0; i < store->count; ++i) {
        if (cs_const_time_memcmp(store->items[i].token_hash, token_hash, sizeof(token_hash) - 1) != 0) {
            continue;
        }

        while (i + 1 < store->count) {
            store->items[i] = store->items[i + 1];
            i += 1;
        }
        memset(&store->items[store->count - 1], 0, sizeof(store->items[store->count - 1]));
        store->count -= 1;
        return 1;
    }

    return 0;
}

static int cs_trust_store_find_token_index(const cs_trust_store *store,
                                           const char *plain_token,
                                           long long now,
                                           long long idle_timeout_seconds,
                                           size_t *index_out) {
    char token_hash[65];
    size_t i;
    long long idle_cutoff = 0;

    if (!store || !cs_plain_token_is_valid(plain_token)) {
        return 0;
    }
    if (cs_hash_token(token_hash, plain_token) != 0) {
        return 0;
    }
    if (idle_timeout_seconds > 0) {
        idle_cutoff = now - idle_timeout_seconds;
    }

    for (i = 0; i < store->count; ++i) {
        const cs_trust_item *item = &store->items[i];

        if (item->expires_at <= now) {
            continue;
        }
        if (idle_timeout_seconds > 0 && item->last_seen_at > 0 && item->last_seen_at <= idle_cutoff) {
            continue;
        }
        if (cs_const_time_memcmp(item->token_hash, token_hash, sizeof(item->token_hash) - 1) == 0) {
            if (index_out) {
                *index_out = i;
            }
            return 1;
        }
    }

    return 0;
}

int cs_trust_store_has_token(const cs_trust_store *store,
                             const char *plain_token,
                             long long now,
                             long long idle_timeout_seconds) {
    return cs_trust_store_find_token_index(store, plain_token, now, idle_timeout_seconds, NULL);
}

int cs_trust_store_touch_token(cs_trust_store *store,
                               const char *plain_token,
                               long long now,
                               long long idle_timeout_seconds,
                               long long touch_granularity_seconds) {
    size_t index;
    cs_trust_item *item;

    if (!store) {
        return -1;
    }
    if (!cs_trust_store_find_token_index(store, plain_token, now, idle_timeout_seconds, &index)) {
        return 0;
    }

    item = &store->items[index];
    if (touch_granularity_seconds > 0
        && item->last_seen_at > 0
        && now - item->last_seen_at < touch_granularity_seconds) {
        return 2;
    }

    item->last_seen_at = now;
    return 1;
}
