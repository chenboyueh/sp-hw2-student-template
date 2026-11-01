#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>
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

int pending_add (PendingTable *table, const char *hash, int origin_fd) {
	if (table->count + 1 > MAX_PENDING) {
		fprintf(stderr, "Pending maxed out.\n");
		return -1;
	}
	strncpy(table->requests[table->count].hash, hash, 16);
	table->requests[table->count].hash[16] = '\0';
	table->requests[table->count].origin_fd = origin_fd;
	table->count++;
	return 0;
}

int pending_pop (PendingTable *table, const char *hash) {
	int n = table->count;
	for (int i = 0; i < n; i++) {
		if (strcmp(table->requests[i].hash, hash) == 0) {
			int fd = table->requests[i].origin_fd;
			table->requests[i] = table->requests[table->count - 1];
			table->count--;
			return fd;
		}
	}
	return -1;
}

int main(int argc, char *argv[]) {
	// TODO: Determine whether the current process is SP main.
  //       After that, you may want to make the following code the same for both
  //       SP main and SP child processes. Therefore, you will set some
  //       variables here.
	int cmd_fd = STDIN_FILENO;
	int reply_fd = STDOUT_FILENO;
	char *cmd_fifo = NULL;
	char *reply_fifo = NULL;

	if (argc > 1) {
    cmd_fifo = argv[1];
		reply_fifo = argv[2];

		if (mkfifo(cmd_fifo, 0666) == -1 && errno != EEXIST) {
			fprintf(stderr, "mkfifo(cmd_fifo) failed.\n");
			return 1;
		}
		if (mkfifo(reply_fifo, 0666) == -1 && errno != EEXIST) {
			fprintf(stderr, "mkfifo(reply_fifo) failed.\n");
			return 1;
		}

		cmd_fd = open(cmd_fifo, O_RDWR | O_CLOEXEC);
		reply_fd = open(reply_fifo, O_RDWR | O_CLOEXEC);
		if (cmd_fd < 0 || reply_fd < 0) {
			fprintf(stderr, "open FIFO failed.\n");
			return 1;
		}
  }

	KVStore store;
	kv_init(&store);

	ChildNodeSet children;
	memset(&children, 0, sizeof(children));
	children.child_count = 0;
	if (argc >= 5) {
		children.parent_read_fd = atoi(argv[3]);
		children.parent_write_fd = atoi(argv[4]);
	} else {
		children.parent_read_fd = -1;
		children.parent_write_fd = -1;
	}

	PendingTable table;
	memset(&table, 0, sizeof(table));
  // end TODO

  while (1) {
		struct pollfd fds[35];
		int nfds = 0;

		// judge channel
		fds[nfds].fd = cmd_fd;
		fds[nfds].events = POLLIN;
		int judge_idx = nfds++;

		// parent channel
		int parent_idx = -1;
		if (children.parent_read_fd != -1) {
			fds[nfds].fd = children.parent_read_fd;
			fds[nfds].events = POLLIN;
			parent_idx = nfds++;
		}

		// children channel
		int child_fd_start = nfds;
		for (int i = 0; i < children.child_count; i++) {
			fds[nfds].fd = children.child[i].read_fd;
			fds[nfds].events = POLLIN;
			nfds++;
		}

		if (poll(fds, nfds, -1) < 0) {
			perror("poll failed.");
			break;
		}

		if (fds[judge_idx].revents & POLLIN) {
			char *command_buffer = NULL;
			ssize_t len = read_command(cmd_fd, &command_buffer);
			if (len <= 0) break;

			Command command = {.buf = command_buffer, .len = len, .type = UNKNOWN};
			classify_command(&command);
			if (command.type == KILL) {
				free(command_buffer);
				break;
			}
			command_handler(&command, &store, reply_fd, &children, &table);
			free(command_buffer);
		}

		if (parent_idx != -1 && (fds[parent_idx].revents & POLLIN)) {
			char* buf = NULL;
			read_command(children.parent_read_fd, &buf);
			if (strncmp(buf, "RESP ", 5) == 0) {
				char *resp_hash = buf + 5;
				char *resp_space_ptr = strchr(resp_hash, ' ');
				if (!resp_space_ptr || resp_space_ptr == resp_hash) {
					fprintf(stderr, "Invalid hash in resp command.\n");
					return -1;
				}
				*resp_space_ptr = '\0';
				char* resp_value = resp_space_ptr + 1;
				if (*resp_value == '\0') {
					fprintf(stderr, "Invalid value in resp command.\n");
					return -1;
				}

				int origin_fd = pending_pop(&table, resp_hash);
				if (origin_fd == -1) { // the lookup is queried by THIS node
					if (dprintf(reply_fd, "%s %s\n", resp_hash, resp_value) < 0) {
						perror("response write to judge failed.");
						return 1;
					}
				} else { // the lookup is queried by children.child[origin_fd]
					if (dprintf(children.child[origin_fd].write_fd, "RESP %s %s\n", resp_hash, resp_value) < 0) {
						perror("response write to children failed.");
						return 1;
					}
				}
			} else if (strncmp(buf, "REQ ", 4) == 0) {
				char *req_hash = buf + 4;
				char *req_space_ptr = strchr(req_hash, ' ');
				if (!req_space_ptr || req_space_ptr == req_hash) {
					fprintf(stderr, "Invalid hash in req command.\n");
					return -1;
				}
				*req_space_ptr = '\0';
				char* req_key = req_space_ptr + 1;
				if (*req_key == '\0') {
					fprintf(stderr, "Invalid key in req command.\n");
					return -1;
				}

				char* req_value = kv_get(&store, req_key);
				if (req_value != NULL) { // hit, response to parent
					if (dprintf(children.parent_write_fd, "RESP %s %s\n", req_hash, req_value) < 0) {
						perror("response write to parent failed.");
						return 1;
					}
				} else { // miss, request all children
					pending_add(&table, req_hash, -2);
					for (int j = 0; j < children.child_count; j++) {
						if (dprintf(children.child[j].write_fd, "REQ %s %s\n", req_hash, req_key) < 0) {
							perror("request write to child failed.");
							return 1;
						}
					}
				}
			} else {
				fprintf(stderr, "Invalid command from parent.\n");
				return 1;
			}
			
			free(buf);
		}

		for (int i = 0; child_fd_start + i < nfds; i++) {
			if (fds[child_fd_start + i].revents & POLLIN) {
				char* buf = NULL;
				read_command(children.child[i].read_fd, &buf);
				if (strncmp(buf, "REQ ", 4) == 0) {
					char *req_hash = buf + 4;
					char *req_space_ptr = strchr(req_hash, ' ');
					if (!req_space_ptr || req_space_ptr == req_hash) {
						fprintf(stderr, "Invalid hash in req command.\n");
						return -1;
					}
					*req_space_ptr = '\0';
					char* req_key = req_space_ptr + 1;
					if (*req_key == '\0') {
						fprintf(stderr, "Invalid key in req command.\n");
						return -1;
					}

					char* req_value = kv_get(&store, req_key);
					if (req_value != NULL) { // hit, response to child[i]
						if (dprintf(children.child[i].write_fd, "RESP %s %s\n", req_hash, req_value) < 0) {
							perror("response write to children failed.");
							return 1;
						}
					} else { // miss, request parent/consultant
						if (children.parent_write_fd == -1) {
							// ask consultant
							return -1;
						} else {
							pending_add(&table, req_hash, i);
							if (dprintf(children.parent_write_fd, "REQ %s %s\n", req_hash, req_key) < 0) {
								perror("request write to parent failed.");
								return 1;
							}
						}
					}
				} else if (strncmp(buf, "RESP ", 5) == 0) {
					char *resp_hash = buf + 5;
					char *resp_space_ptr = strchr(resp_hash, ' ');
					if (!resp_space_ptr || resp_space_ptr == resp_hash) {
						fprintf(stderr, "Invalid hash in resp command.\n");
						return -1;
					}
					*resp_space_ptr = '\0';
					char* resp_value = resp_space_ptr + 1;
					if (*resp_value == '\0') {
						fprintf(stderr, "Invalid value in resp command.\n");
						return -1;
					}

					int origin_fd = pending_pop(&table, resp_hash);
					if (origin_fd == -1) { // the lookup is queried by THIS node
						if (dprintf(reply_fd, "%s %s\n", resp_hash, resp_value) < 0) {
							perror("response write to judge failed.");
							return 1;
						}
					} else if (origin_fd == -2) { // the lookup is queried by parent
						if (dprintf(children.parent_write_fd, "RESP %s %s\n", resp_hash, resp_value) < 0) {
							perror("response write to parent failed.");
							return 1;
						}
					} else { // the lookup is queried by children.child[origin_fd]
						if (dprintf(children.child[origin_fd].write_fd, "RESP %s %s\n", resp_hash, resp_value) < 0) {
							perror("response write to children failed.");
							return 1;
						}
					}
				} else {
					fprintf(stderr, "Invalid command from child.\n");
					return 1;
				}
				
				free(buf);
			}
		}
  }

	kv_free(&store);
	return 0;
}

int command_handler(Command *cmd, KVStore *store, int reply_fd, ChildNodeSet *children, PendingTable *table) {
  switch (cmd->type) {
  case CREATE:
    create_handler(cmd->buf, cmd->len, store, children);
    break;
  case STORE:
    store_handler(cmd->buf, cmd->len, store);
    break;
  case DELETE:
    delete_handler(cmd->buf, cmd->len, store);
    break;
  case FETCH:
    fetch_handler(cmd->buf, cmd->len, store, reply_fd, children, table);
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

int create_handler(char *buf, size_t len, KVStore *store, ChildNodeSet *children) {
  // TODO: Implement the CREATE command handler.
	if (children->child_count + 1 > MAX_CHILDREN) {
		fprintf(stderr, "children maxed out.\n");
		return -1;
	}

	if (len < 10 || strncmp(buf, "create ", 7) != 0) {
		fprintf(stderr, "Invalid create command.\n");
		return -1;
	}
	char *child_cmd_fifo = buf + 7;
	char *child_space_ptr = strchr(child_cmd_fifo, ' ');
	if (!child_space_ptr) {
		fprintf(stderr, "Invalid create command: missing command fifo.\n");
		return -1;
	}
	char *child_reply_fifo = child_space_ptr + 1;
	*child_space_ptr = '\0';
	if (*child_reply_fifo == '\0') {
		fprintf(stderr, "Invalid create command: missing reply fifo.\n");
		return -1;
	}
	
  int p2c[2], c2p[2];
  if (pipe(p2c) == -1 || pipe(c2p) == -1) {
		perror("pipe failed.");
		return -1;
  }

	pid_t pid = fork();
	if (pid < 0) {
		close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
		perror("fork failed.");
		return -1;
	} else if (pid == 0) { // child process
		if (close(p2c[1]) < 0 || close(c2p[0]) < 0) {
			perror("close pipe failed.");
			exit(EXIT_FAILURE);
		}
		fcntl(p2c[0], F_SETFD, 0);
		fcntl(c2p[1], F_SETFD, 0);

		char p2c_read_str[16];
		char c2p_write_str[16];
		snprintf(p2c_read_str, sizeof(p2c_read_str), "%d", p2c[0]);
		snprintf(c2p_write_str, sizeof(c2p_write_str), "%d", c2p[1]);
		char *child_argv[] = {"./sp", child_cmd_fifo, child_reply_fifo, p2c_read_str, c2p_write_str, NULL};
		execv("./sp", child_argv);

		// execv returned -> failed
		close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
		perror("execv failed.");
		return -1;

	} else { // parent process
		if (close(p2c[0]) < 0 || close(c2p[1]) < 0) {
			perror("close pipe failed.");
			return -1;
		}
		if (fcntl(p2c[1], F_SETFD, FD_CLOEXEC) < 0 || fcntl(c2p[0], F_SETFD, FD_CLOEXEC) < 0) {
			close(p2c[0]); close(p2c[1]); close(c2p[0]); close(c2p[1]);
			perror("FD_CLOEXEC set failed.");
			return -1;
		}
		int idx = children->child_count;
		children->child[idx].read_fd = c2p[0];
		children->child[idx].write_fd = p2c[1];
		children->child[idx].pid = pid;
		children->child_count++;
		return 0;
	}
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

int fetch_handler(char *buf, size_t len, KVStore *store, int reply_fd, ChildNodeSet* children, PendingTable *table) {
  // TODO: Implement the FETCH command handler.
	if (len < 8 || strncmp(buf, "fetch ", 6) != 0) {
		fprintf(stderr, "Invalid fetch command.\n");
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
	if (value != NULL) { // local hit
		if (dprintf(reply_fd, "%s %s\n", hash, value) < 0) {
			perror("fetch write failed.");
			return -1;
		}
		return 0;
	}
	
	pending_add(table, hash, -1);
	
	if (children->parent_write_fd != -1) {
		if (dprintf(children->parent_write_fd, "REQ %s %s\n", hash, key) < 0) {
			perror("request write to parent failed.");
			return -1;
		}
	}
	
	for (int i = 0; i < children->child_count; i++) {
		if (dprintf(children->child[i].write_fd, "REQ %s %s\n", hash, key) < 0) {
			perror("request write to child failed.");
			return -1;
		}
	}

	return 0;
}

int kill_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the KILL command handler.
}

int update_handler(char *buf, size_t len, KVStore *store) {
  // TODO: Implement the UPDATE command handler.
}
