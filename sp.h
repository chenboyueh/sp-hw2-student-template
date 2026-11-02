#ifndef SP_H
#define SP_H

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "kv_store.h"

typedef struct {
  pid_t pid;
  int read_fd;
  int write_fd;
} ChildNode;

#define MAX_CHILDREN 32

typedef struct {
  ChildNode child[MAX_CHILDREN];
	int child_count;
	int parent_read_fd;
	int parent_write_fd;
} ChildNodeSet;


typedef struct {
  char hash[17];
  int origin_fd;  // judge = -1; parent = -2
} PendingFetch;

#define MAX_PENDING 1024

typedef struct {
  PendingFetch requests[MAX_PENDING];
  int count;
} PendingTable;


typedef struct {
  char *buf;
  size_t len;
  enum { CREATE, STORE, DELETE, FETCH, KILL, UPDATE, UNKNOWN } type;
} Command;

int command_handler(Command *cmd, KVStore *store, int reply_fd, ChildNodeSet *children, PendingTable *table, char **consultant_path, char *cmd_fifo, char *reply_fifo);
int create_handler(char *buf, size_t len, KVStore *store, ChildNodeSet *children);
int store_handler(char *buf, size_t len, KVStore *store);
int delete_handler(char *buf, size_t len, KVStore *store);
int fetch_handler(char *buf, size_t len, KVStore *store, int reply_fd, ChildNodeSet *children, PendingTable *table, char **consultant_path);
void kill_handler(ChildNodeSet *children, char *cmd_fifo, char *reply_fifo, KVStore *store, char **consultant_path);
int update_handler(char *buf, size_t len, char **consultant_path);

#endif // SP_H