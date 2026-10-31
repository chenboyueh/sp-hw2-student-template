#ifndef SP_H
#define SP_H

#include <stdio.h>
#include "kv_store.h"

typedef struct {
  char *buf;
  size_t len;
  enum { CREATE, STORE, DELETE, FETCH, KILL, UPDATE, UNKNOWN } type;
} Command;

int command_handler(Command *cmd, KVStore *store, int reply_fd);
int create_handler(char *buf, size_t len, KVStore *store);
int store_handler(char *buf, size_t len, KVStore *store);
int delete_handler(char *buf, size_t len, KVStore *store);
int fetch_handler(char *buf, size_t len, KVStore *store, int reply_fd);
int kill_handler(char *buf, size_t len, KVStore *store);
int update_handler(char *buf, size_t len, KVStore *store);

#endif // SP_H