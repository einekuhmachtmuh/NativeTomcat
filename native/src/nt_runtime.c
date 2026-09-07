#include "nt_runtime.h"

#include "nt_connection.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

struct nt_runtime_command {
	uint64_t handle;
	bool want_write;
	bool close;
	struct nt_runtime_command *next;
};

struct nt_runtime {
	int listener_fd;
	int epoll_fd;
	int wake_fd;
	int max_events;
	nt_runtime_connection_handler_t connection_handler;
	void *connection_handler_data;
	atomic_bool stop_requested;
	nt_connection_t **connections;
	size_t connection_count;
	size_t connection_capacity;
	pthread_mutex_t connection_mutex;
	pthread_mutex_t command_mutex;
	struct nt_runtime_command *command_head;
	struct nt_runtime_command *command_tail;
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
	if (pthread_mutex_lock(&runtime->connection_mutex) != 0) {
		errno = EBUSY;
		return -1;
	}

	int status = 0;
	if (runtime->connection_count == runtime->connection_capacity) {
		size_t new_capacity = runtime->connection_capacity == 0 ? 64 : runtime->connection_capacity * 2;
		if (new_capacity < runtime->connection_capacity ||
				new_capacity > SIZE_MAX / sizeof(*runtime->connections)) {
			errno = EOVERFLOW;
			status = -1;
		} else {
			nt_connection_t **connections = realloc(
					runtime->connections, new_capacity * sizeof(*runtime->connections));
			if (connections == NULL) {
				status = -1;
			} else {
				runtime->connections = connections;
				runtime->connection_capacity = new_capacity;
			}
		}
	}

	if (status == 0) {
		runtime->connections[runtime->connection_count++] = connection;
	}
	pthread_mutex_unlock(&runtime->connection_mutex);
	return status;
}

static void nt_runtime_close_connections(nt_runtime_t *runtime) {
	if (pthread_mutex_lock(&runtime->connection_mutex) != 0) {
		return;
	}
	for (size_t i = 0; i < runtime->connection_count; ++i) {
		nt_connection_close(runtime->connections[i]);
	}
	pthread_mutex_unlock(&runtime->connection_mutex);
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

static int nt_runtime_wake(nt_runtime_t *runtime) {
	uint64_t value = 1;
	ssize_t result = write(runtime->wake_fd, &value, sizeof(value));
	if (result == (ssize_t) sizeof(value) || (result == -1 && errno == EAGAIN)) {
		return 0;
	}
	return -1;
}

static int nt_runtime_enqueue_command(nt_runtime_t *runtime, uint64_t handle, bool want_write, bool close) {
	if (runtime == NULL || handle == 0 || (want_write && close)) {
		errno = EINVAL;
		return -1;
	}
	if (atomic_load_explicit(&runtime->stop_requested, memory_order_acquire)) {
		errno = ECANCELED;
		return -1;
	}

	struct nt_runtime_command *command = calloc(1, sizeof(*command));
	if (command == NULL) {
		return -1;
	}
	command->handle = handle;
	command->want_write = want_write;
	command->close = close;

	if (pthread_mutex_lock(&runtime->command_mutex) != 0) {
		free(command);
		errno = EBUSY;
		return -1;
	}
	if (atomic_load_explicit(&runtime->stop_requested, memory_order_acquire)) {
		pthread_mutex_unlock(&runtime->command_mutex);
		free(command);
		errno = ECANCELED;
		return -1;
	}
	if (runtime->command_tail == NULL) {
		runtime->command_head = command;
	} else {
		runtime->command_tail->next = command;
	}
	runtime->command_tail = command;
	pthread_mutex_unlock(&runtime->command_mutex);

	if (nt_runtime_wake(runtime) != 0) {
		return -1;
	}
	return 0;
}

static struct nt_runtime_command *nt_runtime_take_commands(nt_runtime_t *runtime) {
	if (pthread_mutex_lock(&runtime->command_mutex) != 0) {
		return NULL;
	}
	struct nt_runtime_command *commands = runtime->command_head;
	runtime->command_head = NULL;
	runtime->command_tail = NULL;
	pthread_mutex_unlock(&runtime->command_mutex);
	return commands;
}

static void nt_runtime_free_commands(struct nt_runtime_command *commands) {
	while (commands != NULL) {
		struct nt_runtime_command *next = commands->next;
		free(commands);
		commands = next;
	}
}

static void nt_runtime_process_commands(nt_runtime_t *runtime) {
	struct nt_runtime_command *commands = nt_runtime_take_commands(runtime);
	struct nt_runtime_command *effective_head = NULL;
	struct nt_runtime_command *effective_tail = NULL;

	while (commands != NULL) {
		struct nt_runtime_command *command = commands;
		commands = commands->next;
		command->next = NULL;

		struct nt_runtime_command *existing = effective_head;
		while (existing != NULL && existing->handle != command->handle) {
			existing = existing->next;
		}
		if (existing == NULL) {
			if (effective_tail == NULL) {
				effective_head = command;
			} else {
				effective_tail->next = command;
			}
			effective_tail = command;
		} else if (command->close) {
			existing->close = true;
			existing->want_write = false;
		} else if (!existing->close) {
			existing->want_write = command->want_write;
		}
		free(command);
	}

	struct nt_runtime_command *command = effective_head;
	while (command != NULL) {
		struct nt_runtime_command *next = command->next;
		nt_connection_t *connection = nt_runtime_find_connection(runtime, command->handle);
		if (connection != NULL && nt_connection_get_state(connection) == NT_CONNECTION_ACTIVE) {
			if (command->close) {
				if (epoll_ctl(runtime->epoll_fd, EPOLL_CTL_DEL, nt_connection_get_fd(connection), NULL) == -1 &&
						errno != ENOENT && errno != EBADF) {
					/* The connection is still closed below; epoll cleanup is best effort. */
				}
				nt_connection_close(connection);
			} else {
				(void)nt_runtime_rearm_connection(runtime, connection, command->want_write);
			}
		}
		free(command);
		command = next;
	}
}

static void nt_runtime_discard_commands(nt_runtime_t *runtime) {
	nt_runtime_free_commands(nt_runtime_take_commands(runtime));
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
	value->connection_handler = config->connection_handler;
	value->connection_handler_data = config->connection_handler_data;
	atomic_init(&value->stop_requested, false);

	if (pthread_mutex_init(&value->connection_mutex, NULL) != 0) {
		free(value);
		return -1;
	}
	if (pthread_mutex_init(&value->command_mutex, NULL) != 0) {
		pthread_mutex_destroy(&value->connection_mutex);
		free(value);
		return -1;
	}

	value->listener_fd = nt_create_listener(config);
	if (value->listener_fd == -1) {
		pthread_mutex_destroy(&value->command_mutex);
		pthread_mutex_destroy(&value->connection_mutex);
		free(value);
		return -1;
	}

	value->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (value->epoll_fd == -1) {
		close(value->listener_fd);
		pthread_mutex_destroy(&value->command_mutex);
		pthread_mutex_destroy(&value->connection_mutex);
		free(value);
		return -1;
	}

	value->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (value->wake_fd == -1) {
		close(value->epoll_fd);
		close(value->listener_fd);
		pthread_mutex_destroy(&value->command_mutex);
		pthread_mutex_destroy(&value->connection_mutex);
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
		pthread_mutex_destroy(&value->command_mutex);
		pthread_mutex_destroy(&value->connection_mutex);
		free(value);
		return -1;
	}

	event.data.ptr = &value->wake_token;
	if (epoll_ctl(value->epoll_fd, EPOLL_CTL_ADD, value->wake_fd, &event) == -1) {
		close(value->wake_fd);
		close(value->epoll_fd);
		close(value->listener_fd);
		pthread_mutex_destroy(&value->command_mutex);
		pthread_mutex_destroy(&value->connection_mutex);
		free(value);
		return -1;
	}

	*runtime = value;
	return 0;
}

int nt_runtime_get_port(const nt_runtime_t *runtime) {
	if (runtime == NULL || runtime->listener_fd == -1) {
		errno = EINVAL;
		return -1;
	}

	struct sockaddr_in address;
	socklen_t length = sizeof(address);
	if (getsockname(runtime->listener_fd, (struct sockaddr *) &address, &length) == -1) {
		return -1;
	}

	return (int) ntohs(address.sin_port);
}

nt_connection_t *nt_runtime_find_connection(nt_runtime_t *runtime, uint64_t handle) {
	if (runtime == NULL || handle == 0) {
		errno = EINVAL;
		return NULL;
	}

	if (pthread_mutex_lock(&runtime->connection_mutex) != 0) {
		errno = EBUSY;
		return NULL;
	}

	nt_connection_t *result = NULL;
	for (size_t i = 0; i < runtime->connection_count; ++i) {
		nt_connection_t *connection = runtime->connections[i];
		if (nt_connection_get_handle(connection) == handle) {
			result = connection;
			break;
		}
	}
	pthread_mutex_unlock(&runtime->connection_mutex);

	if (result == NULL) {
		errno = ENOENT;
	}
	return result;
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

int nt_runtime_request_rearm(nt_runtime_t *runtime, uint64_t handle, bool want_write) {
	return nt_runtime_enqueue_command(runtime, handle, want_write, false);
}

int nt_runtime_request_close(nt_runtime_t *runtime, uint64_t handle) {
	return nt_runtime_enqueue_command(runtime, handle, false, true);
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
				nt_runtime_process_commands(runtime);
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

			unsigned connection_events = 0;
			if ((events[i].events & EPOLLIN) != 0) {
				connection_events |= NT_RUNTIME_EVENT_READABLE;
			}
			if ((events[i].events & EPOLLOUT) != 0) {
				connection_events |= NT_RUNTIME_EVENT_WRITABLE;
			}
			if ((events[i].events & (EPOLLRDHUP | EPOLLHUP)) != 0) {
				nt_connection_mark_peer_read_closed(connection);
				connection_events |= NT_RUNTIME_EVENT_PEER_READ_CLOSED;
			}
			if ((events[i].events & EPOLLERR) != 0) {
				connection_events |= NT_RUNTIME_EVENT_ERROR;
			}

			if (runtime->connection_handler != NULL && connection_events != 0) {
				runtime->connection_handler(runtime, connection, connection_events,
					runtime->connection_handler_data);
			}
		}
	}

	free(events);
	nt_runtime_discard_commands(runtime);
	nt_runtime_close_connections(runtime);
	return 0;
}

void nt_runtime_stop(nt_runtime_t *runtime) {
	if (runtime != NULL) {
		atomic_store_explicit(&runtime->stop_requested, true, memory_order_release);
		if (runtime->wake_fd != -1) {
			(void)nt_runtime_wake(runtime);
		}
	}
}

void nt_runtime_destroy(nt_runtime_t *runtime) {
	if (runtime == NULL) {
		return;
	}

	nt_runtime_discard_commands(runtime);
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
	pthread_mutex_destroy(&runtime->command_mutex);
	pthread_mutex_destroy(&runtime->connection_mutex);
	free(runtime);
}
