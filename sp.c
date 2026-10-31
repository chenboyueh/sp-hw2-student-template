#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kv_store.h"		

ssize_t read_command (int fd, char **out_buf) {
	size_t capacity = 128;
	size_t len = 0;
	char *buf = malloc(capacity);
	if (!buf) return -1;

	while (1) {
		char ch;
		ssize_t n = read(fd, &ch, 1);
		if (n <= 0) {
			if (len == 0) {
				free(buf);
				return -1;
			}
			break;
		}
		if (ch == '\n') break;
		if (len + 1 >= capacity) {
			capacity *= 2;
			char *new = realloc(buf, capacity);
			if (!new) {
				free(buf);
				return -1;
			}
			buf = new;
		}
		buf[len++] = ch;
	}
	buf[len] = '\0';
	*out_buf = buf;
	return len;
}

void classify_command (Command* cmd) {
	if (!cmd->buf) return;
	if (strncmp(cmd->buf, "store ", 6) == 0) cmd->type = STORE;
	else if (strncmp(cmd->buf, "delete ", 7) == 0) cmd->type = DELETE;
	else if (strncmp(cmd->buf, "fetch ", 6) == 0) cmd->type = FETCH;
	else if (strncmp(cmd->buf, "create ", 7) == 0) cmd->type = CREATE;
	else if (strcmp(cmd->buf, "kill") == 0) cmd->type = KILL;
	else if (strncmp(cmd->buf, "update ", 7) == 0) cmd->type = UPDATE;
	else cmd->type = UNKNOWN;
}

int main(int argc, char *argv[]) {
	// TODO: Determine whether the current process is SP main.
  //       After that, you may want to make the following code the same for both
  //       SP main and SP child processes. Therefore, you will set some
  //       variables here.
	int cmd_fd = STDIN_FILENO;
	int reply_fd = STDOUT_FILENO;

	if (argc > 1) {
    char *cmd_fifo = argv[1];
		char *reply_fifo = argv[2];

		if (mkfifo(cmd_fifo, 0666) == -1) {
			fprintf(stderr, "mkfifo(cmd_fifo) failed.\n");
			return 0;
		}
		if (mkfifo(reply_fifo, 0666) == -1) {
			fprintf(stderr, "mkfifo(reply_fifo) failed.\n");
			return 0;
		}

		cmd_fd = open(cmd_fifo, O_RDONLY | O_CLOEXEC);
		reply_fd = open(reply_fifo, O_WRONLY | O_CLOEXEC);
		if (cmd_fd < 0 || reply_fd < 0) {
			fprintf(stderr, "open FIFO failed.\n");
			return 1;
		}
  }

	KVStore store;
	kv_init(&store);
  // end TODO

  while (1) {
    char *command_buffer = NULL;
    Command command = {.buf = command_buffer, .len = 0, .type = UNKNOWN};
    // TODO: poll the STDIO/FIFO for commands.

    // end TODO

    // TODO: poll parent, and child processes for custom commands.

    // end TODO

    // TODO: classify the command and execute it.
		ssize_t len = read_command(cmd_fd, &command_buffer);
		if (len < 0) break;
		command.buf = command_buffer;
		command.len = len;
		classify_command(&command);
		if (command.type == KILL) {
			free(command_buffer);
			break;
		}
    command_handler(&command, &store, reply_fd);
		free(command_buffer);
    // end TODO

    // TODO: handle custom commands.

    // end TODO
  }

	kv_free(&store);
	return 0;
}

int command_handler(Command *cmd, KVStore *store, int reply_fd) {
  switch (cmd->type) {
  case CREATE:
    create_handler(cmd->buf, cmd->len, store);
    break;
  case STORE:
    store_handler(cmd->buf, cmd->len, store);
    break;
  case DELETE:
    delete_handler(cmd->buf, cmd->len, store);
    break;
  case FETCH:
    fetch_handler(cmd->buf, cmd->len, store, reply_fd);
    break;
  case KILL:
    kill_handler(cmd->buf, cmd->len, store);
    break;
  case UPDATE:
    update_handler(cmd->buf, cmd->len, store);
    break;
  default:
    fprintf(stderr, "Unknown command type\n");
    return -1;
  }
  return 0;
}

int create_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the CREATE command handler.
}

int store_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the STORE command handler.
	if (len < 8 || strncmp(buf, "store ", 6) != 0) {
		fprintf(stderr, "Invalid store command.\n");
		return -1;
	}
	char *key = buf + 6;
	char *space_ptr = strchr(key, ' ');
	if (!space_ptr || space_ptr == key) {
		fprintf(stderr, "Invalid key in store command.\n");
		return -1;
	}
	*space_ptr = '\0';
	char* value = space_ptr + 1;
	if (*value == '\0') {
		fprintf(stderr, "Invalid value in store command.\n");
		return -1;
	}

	if (kv_set(store, key, value) == -1) {
		fprintf(stderr, "kv_set realloc failed.\n");
		return -1;
	}
	return 0;
}

int delete_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the DELETE command handler.
	if (len < 8 || strncmp(buf, "delete ", 7) != 0) {
		fprintf(stderr, "Invalid delete command.\n");
		return -1;
	}
	char *key = buf + 7;
	if (*key == '\0') {
		fprintf(stderr, "Invalid key in delete command.\n");
		return -1;
	}
	kv_delete(store, key);
	return 0;
}

int fetch_handler(char *buf, size_t len, KVStore *store, int reply_fd) {
  // TODO: Implement the FETCH command handler.
	if (len < 8 || strncmp(buf, "fetch ", 6) != 0) {
		fprintf(stderr, "Invalid store command.\n");
		return -1;
	}
	char *hash = buf + 6;
	char *space_ptr = strchr(hash, ' ');
	if (!space_ptr || space_ptr == hash) {
		fprintf(stderr, "Invalid hash in fetch command.\n");
		return -1;
	}
	*space_ptr = '\0';
	char* key = space_ptr + 1;
	if (*key == '\0') {
		fprintf(stderr, "Invalid key in fetch command.\n");
		return -1;
	}
	
	const char *value = kv_get(store, key);
	if (value == NULL) {
		// ask ancestor or consultant
		return -1;
	}

	if (dprintf(reply_fd, "%s %s\n", hash, value) < 0) {
		perror("fetch write failed.");
		return -1;
	}
	return 0;
}

int kill_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the KILL command handler.
}

int update_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the UPDATE command handler.
}
