#include "nt_runtime.h"

#include "nt_connection.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

struct nt_runtime {
	int listener_fd;
	int epoll_fd;
	int wake_fd;
	int max_events;
	atomic_bool stop_requested;
	nt_connection_t **connections;
	size_t connection_count;
	size_t connection_capacity;
	int listener_token;
	int wake_token;
};

static int nt_set_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1) {
		return -1;
	}
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int nt_create_listener(const nt_runtime_config_t *config) {
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd == -1) {
		return -1;
	}

	int reuse = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
		close(fd);
		return -1;
	}

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(config->port);
	if (config->bind_address == NULL || inet_pton(AF_INET, config->bind_address, &address.sin_addr) != 1) {
		close(fd);
		errno = EINVAL;
		return -1;
	}

	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
			listen(fd, config->backlog) == -1 || nt_set_nonblocking(fd) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

static int nt_runtime_add_connection(nt_runtime_t *runtime, nt_connection_t *connection) {
	if (runtime->connection_count == runtime->connection_capacity) {
		size_t new_capacity = runtime->connection_capacity == 0 ? 64 : runtime->connection_capacity * 2;
		if (new_capacity < runtime->connection_capacity ||
				new_capacity > SIZE_MAX / sizeof(*runtime->connections)) {
			errno = EOVERFLOW;
			return -1;
		}

		nt_connection_t **connections = realloc(
				runtime->connections, new_capacity * sizeof(*runtime->connections));
		if (connections == NULL) {
			return -1;
		}
		runtime->connections = connections;
		runtime->connection_capacity = new_capacity;
	}

	runtime->connections[runtime->connection_count++] = connection;
	return 0;
}

static void nt_runtime_close_connections(nt_runtime_t *runtime) {
	for (size_t i = 0; i < runtime->connection_count; ++i) {
		nt_connection_close(runtime->connections[i]);
	}
}

static void nt_runtime_destroy_connections(nt_runtime_t *runtime) {
	for (size_t i = 0; i < runtime->connection_count; ++i) {
		nt_connection_destroy(runtime->connections[i]);
	}
	free(runtime->connections);
	runtime->connections = NULL;
	runtime->connection_count = 0;
	runtime->connection_capacity = 0;
}

static void nt_runtime_accept_connections(nt_runtime_t *runtime) {
	for (;;) {
		int client = accept4(runtime->listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (client == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			if (errno == EINTR) {
				continue;
			}
			break;
		}

		nt_connection_t *connection = NULL;
		if (nt_connection_create(&connection, runtime, client) == -1) {
			close(client);
			continue;
		}

		struct epoll_event event;
		memset(&event, 0, sizeof(event));
		event.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
		event.data.ptr = connection;
		if (epoll_ctl(runtime->epoll_fd, EPOLL_CTL_ADD, client, &event) == -1) {
			nt_connection_destroy(connection);
			continue;
		}

		if (nt_runtime_add_connection(runtime, connection) == -1) {
			epoll_ctl(runtime->epoll_fd, EPOLL_CTL_DEL, client, NULL);
			nt_connection_destroy(connection);
			continue;
		}
	}
}

int nt_runtime_init(nt_runtime_t **runtime, const nt_runtime_config_t *config) {
	if (runtime == NULL || config == NULL || config->max_events <= 0 || config->backlog <= 0) {
		errno = EINVAL;
		return -1;
	}

	nt_runtime_t *value = calloc(1, sizeof(*value));
	if (value == NULL) {
		return -1;
	}

	value->listener_fd = -1;
	value->epoll_fd = -1;
	value->wake_fd = -1;
	value->max_events = config->max_events;
	atomic_init(&value->stop_requested, false);

	value->listener_fd = nt_create_listener(config);
	if (value->listener_fd == -1) {
		free(value);
		return -1;
	}

	value->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (value->epoll_fd == -1) {
		close(value->listener_fd);
		free(value);
		return -1;
	}

	value->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (value->wake_fd == -1) {
		close(value->epoll_fd);
		close(value->listener_fd);
		free(value);
		return -1;
	}

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.ptr = &value->listener_token;
	if (epoll_ctl(value->epoll_fd, EPOLL_CTL_ADD, value->listener_fd, &event) == -1) {
		close(value->wake_fd);
		close(value->epoll_fd);
		close(value->listener_fd);
		free(value);
		return -1;
	}

	event.data.ptr = &value->wake_token;
	if (epoll_ctl(value->epoll_fd, EPOLL_CTL_ADD, value->wake_fd, &event) == -1) {
		close(value->wake_fd);
		close(value->epoll_fd);
		close(value->listener_fd);
		free(value);
		return -1;
	}

	*runtime = value;
	return 0;
}

int nt_runtime_rearm_connection(nt_runtime_t *runtime, nt_connection_t *connection, bool want_write) {
	if (runtime == NULL || connection == NULL || nt_connection_get_runtime(connection) != runtime) {
		errno = EINVAL;
		return -1;
	}
	if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
		errno = EBADF;
		return -1;
	}

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
	if (want_write) {
		event.events |= EPOLLOUT;
	}
	event.data.ptr = connection;
	return epoll_ctl(runtime->epoll_fd, EPOLL_CTL_MOD, nt_connection_get_fd(connection), &event);
}

int nt_runtime_run(nt_runtime_t *runtime) {
	if (runtime == NULL) {
		errno = EINVAL;
		return -1;
	}

	struct epoll_event *events = calloc((size_t)runtime->max_events, sizeof(*events));
	if (events == NULL) {
		return -1;
	}

	while (!atomic_load_explicit(&runtime->stop_requested, memory_order_acquire)) {
		int count = epoll_wait(runtime->epoll_fd, events, runtime->max_events, -1);
		if (count == -1) {
			if (errno == EINTR) {
				continue;
			}
			free(events);
			return -1;
		}

		for (int i = 0; i < count; ++i) {
			if (events[i].data.ptr == &runtime->wake_token) {
				uint64_t value;
				while (read(runtime->wake_fd, &value, sizeof(value)) == sizeof(value)) {
				}
				continue;
			}

			if (events[i].data.ptr == &runtime->listener_token) {
				nt_runtime_accept_connections(runtime);
				continue;
			}

			nt_connection_t *connection = events[i].data.ptr;
			if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
				continue;
			}

			if ((events[i].events & EPOLLERR) != 0) {
				nt_connection_close(connection);
				continue;
			}

			if ((events[i].events & (EPOLLRDHUP | EPOLLHUP)) != 0) {
				nt_connection_mark_peer_read_closed(connection);
			}
		}
	}

	free(events);
	nt_runtime_close_connections(runtime);
	return 0;
}

void nt_runtime_stop(nt_runtime_t *runtime) {
	if (runtime != NULL) {
		atomic_store_explicit(&runtime->stop_requested, true, memory_order_release);
		if (runtime->wake_fd != -1) {
			uint64_t value = 1;
			ssize_t result = write(runtime->wake_fd, &value, sizeof(value));
			(void)result;
		}
	}
}

void nt_runtime_destroy(nt_runtime_t *runtime) {
	if (runtime == NULL) {
		return;
	}

	nt_runtime_destroy_connections(runtime);
	if (runtime->wake_fd != -1) {
		close(runtime->wake_fd);
	}
	if (runtime->epoll_fd != -1) {
		close(runtime->epoll_fd);
	}
	if (runtime->listener_fd != -1) {
		close(runtime->listener_fd);
	}
	free(runtime);
}
