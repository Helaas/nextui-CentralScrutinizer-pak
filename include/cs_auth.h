#ifndef CS_AUTH_H
#define CS_AUTH_H

#include <stddef.h>

#define CS_TRUST_STORE_CAPACITY 32

typedef struct cs_pairing {
    char code[5];
} cs_pairing;

typedef struct cs_trust_item {
    char browser_id[128];
    char token_hash[65];
    long long expires_at;
    long long last_seen_at;
} cs_trust_item;

typedef struct cs_trust_store {
    cs_trust_item items[CS_TRUST_STORE_CAPACITY];
    size_t count;
} cs_trust_store;

int cs_pairing_generate(cs_pairing *pairing);
int cs_const_time_memcmp(const void *left, const void *right, size_t len);
int cs_trust_store_load(const char *path, cs_trust_store *store);
int cs_trust_store_add(cs_trust_store *store, const char *browser_id, const char *plain_token);
int cs_trust_store_save(const char *path, const cs_trust_store *store);
int cs_trust_store_remove_expired(cs_trust_store *store, long long now);
int cs_trust_store_remove_token(cs_trust_store *store, const char *plain_token);
int cs_trust_store_has_token(const cs_trust_store *store,
                             const char *plain_token,
                             long long now,
                             long long idle_timeout_seconds);
int cs_trust_store_touch_token(cs_trust_store *store,
                               const char *plain_token,
                               long long now,
                               long long idle_timeout_seconds,
                               long long touch_granularity_seconds);

#endif
