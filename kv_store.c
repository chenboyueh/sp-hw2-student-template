#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kv_store.h"

void kv_init(KVStore *store) {
    store->pairs = NULL;
    store->count = 0;
    store->capacity = 0;
}

void kv_free(KVStore *store) {
    int cnt = store->count;
    for (int i = 0; i < cnt; i++) {
        free(store->pairs[i].value);
        free(store->pairs[i].key);
    }
    free(store->pairs);
    store->capacity = 0;
    store->count = 0;
    store->pairs = NULL;
}

int kv_set(KVStore *store, const char *key, const char *value) {
    int cnt = store->count;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(key, store->pairs[i].key) == 0) {
            free(store->pairs[i].value);
            store->pairs[i].value = strdup(value);
            return 0;
        }
    }
    if (store->count == store->capacity) {
        size_t new_capacity = (store->capacity > 0)? (2 * store->capacity) : 8;
        KVPair *new_pairs = realloc(store->pairs, sizeof(KVPair) * new_capacity);
        if (!new_pairs) return -1;
        store->pairs = new_pairs;
        store->capacity = new_capacity;
    }
    store->pairs[store->count].key = strdup(key);
    store->pairs[store->count].value = strdup(value);
    (store->count)++;
    return 0;
}

const char *kv_get(KVStore *store, const char *key) {
    int cnt = store->count;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(key, store->pairs[i].key) == 0) {
            return store->pairs[i].value;
        }
    }
    return NULL;
}

int kv_delete(KVStore *store, const char *key) {
    int cnt = store->count;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(key, store->pairs[i].key) == 0) {
            free(store->pairs[i].key);
            free(store->pairs[i].value);
            if (i != cnt - 1) {
                store->pairs[i] = store->pairs[cnt - 1];
            }
            (store->count)--;
            return 0;
        }
    }
    return 0;
}