#include "nt_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

struct nt_runtime {
	int listener_fd;
	int epoll_fd;
	int max_events;
	volatile sig_atomic_t stop_requested;
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
	value->max_events = config->max_events;
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

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = value->listener_fd;
	if (epoll_ctl(value->epoll_fd, EPOLL_CTL_ADD, value->listener_fd, &event) == -1) {
		close(value->epoll_fd);
		close(value->listener_fd);
		free(value);
		return -1;
	}

	*runtime = value;
	return 0;
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

	while (!runtime->stop_requested) {
		int count = epoll_wait(runtime->epoll_fd, events, runtime->max_events, -1);
		if (count == -1) {
			if (errno == EINTR) {
				continue;
			}
			free(events);
			return -1;
		}

		for (int i = 0; i < count; ++i) {
			if (events[i].data.fd != runtime->listener_fd) {
				continue;
			}

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
				close(client);
			}
		}
	}

	free(events);
	return 0;
}

void nt_runtime_stop(nt_runtime_t *runtime) {
	if (runtime != NULL) {
		runtime->stop_requested = 1;
	}
}

void nt_runtime_destroy(nt_runtime_t *runtime) {
	if (runtime == NULL) {
		return;
	}
	if (runtime->epoll_fd != -1) {
		close(runtime->epoll_fd);
	}
	if (runtime->listener_fd != -1) {
		close(runtime->listener_fd);
	}
	free(runtime);
}
