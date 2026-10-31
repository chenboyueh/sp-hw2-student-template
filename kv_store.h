#ifndef KV_STORE_H
#define KV_STORE_H

#include <stddef.h>

typedef struct {
    char *key;
    char *value;
} KVPair;

typedef struct {
    KVPair *pairs;
    size_t count;
    size_t capacity;
} KVStore;

void kv_init(KVStore *store);
void kv_free(KVStore *store);
int kv_set(KVStore *store, const char *key, const char *value);
const char *kv_get(KVStore *store, const char *key);
int kv_delete(KVStore *store, const char *key);

#endif