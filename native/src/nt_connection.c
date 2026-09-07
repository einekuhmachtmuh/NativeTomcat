#include "nt_connection.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

struct nt_connection {
	nt_runtime_t *runtime;
	uint64_t handle;
	int fd;
	atomic_int state;
	atomic_bool peer_read_closed;
};

static atomic_uint_fast64_t nt_next_connection_handle = 1;

int nt_connection_create(nt_connection_t **connection, nt_runtime_t *runtime, int fd) {
	if (connection == NULL || runtime == NULL || fd < 0) {
		errno = EINVAL;
		return -1;
	}

	nt_connection_t *value = malloc(sizeof(*value));
	if (value == NULL) {
		return -1;
	}

	uint64_t handle = atomic_fetch_add_explicit(&nt_next_connection_handle, 1, memory_order_relaxed);
	if (handle == 0) {
		free(value);
		errno = EOVERFLOW;
		return -1;
	}

	value->runtime = runtime;
	value->handle = handle;
	value->fd = fd;
	atomic_init(&value->state, NT_CONNECTION_ACTIVE);
	atomic_init(&value->peer_read_closed, false);
	*connection = value;
	return 0;
}

uint64_t nt_connection_get_handle(const nt_connection_t *connection) {
	if (connection == NULL) {
		return 0;
	}
	return connection->handle;
}

int nt_connection_get_fd(const nt_connection_t *connection) {
	if (connection == NULL) {
		errno = EINVAL;
		return -1;
	}
	return connection->fd;
}

nt_runtime_t *nt_connection_get_runtime(const nt_connection_t *connection) {
	if (connection == NULL) {
		return NULL;
	}
	return connection->runtime;
}

nt_connection_state_t nt_connection_get_state(const nt_connection_t *connection) {
	if (connection == NULL) {
		return NT_CONNECTION_CLOSED;
	}
	return (nt_connection_state_t)atomic_load_explicit(&connection->state, memory_order_acquire);
}

bool nt_connection_peer_read_closed(const nt_connection_t *connection) {
	if (connection == NULL) {
		return true;
	}
	return atomic_load_explicit(&connection->peer_read_closed, memory_order_acquire);
}

void nt_connection_mark_peer_read_closed(nt_connection_t *connection) {
	if (connection != NULL) {
		atomic_store_explicit(&connection->peer_read_closed, true, memory_order_release);
	}
}

nt_connection_io_result_t nt_connection_read(nt_connection_t *connection, void *buffer, size_t length, ssize_t *result) {
	if (connection == NULL || result == NULL || (buffer == NULL && length != 0)) {
		errno = EINVAL;
		return NT_CONNECTION_IO_ERROR;
	}
	if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
		errno = EBADF;
		return NT_CONNECTION_IO_ERROR;
	}

	for (;;) {
		ssize_t value = recv(connection->fd, buffer, length, 0);
		if (value > 0) {
			*result = value;
			return NT_CONNECTION_IO_OK;
		}
		if (value == 0) {
			*result = 0;
			nt_connection_mark_peer_read_closed(connection);
			return NT_CONNECTION_IO_EOF;
		}

		int error = errno;
		if (error == EINTR) {
			continue;
		}
		if (error == EAGAIN || error == EWOULDBLOCK) {
			errno = error;
			return NT_CONNECTION_IO_WOULD_BLOCK;
		}
		if (error == EBADF || error == ECONNRESET || error == ENOTCONN) {
			nt_connection_close(connection);
		}
		errno = error;
		return NT_CONNECTION_IO_ERROR;
	}
}

nt_connection_io_result_t nt_connection_write(nt_connection_t *connection, const void *buffer, size_t length, ssize_t *result) {
	if (connection == NULL || result == NULL || (buffer == NULL && length != 0)) {
		errno = EINVAL;
		return NT_CONNECTION_IO_ERROR;
	}
	if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
		errno = EBADF;
		return NT_CONNECTION_IO_ERROR;
	}

	for (;;) {
		ssize_t value = send(connection->fd, buffer, length, MSG_NOSIGNAL);
		if (value >= 0) {
			*result = value;
			return NT_CONNECTION_IO_OK;
		}

		int error = errno;
		if (error == EINTR) {
			continue;
		}
		if (error == EAGAIN || error == EWOULDBLOCK) {
			errno = error;
			return NT_CONNECTION_IO_WOULD_BLOCK;
		}
		if (error == EBADF || error == ECONNRESET || error == EPIPE || error == ENOTCONN) {
			nt_connection_close(connection);
		}
		errno = error;
		return NT_CONNECTION_IO_ERROR;
	}
}

void nt_connection_close(nt_connection_t *connection) {
	if (connection == NULL) {
		return;
	}

	int expected = NT_CONNECTION_ACTIVE;
	if (atomic_compare_exchange_strong_explicit(&connection->state, &expected, NT_CONNECTION_CLOSING,
			memory_order_acq_rel, memory_order_acquire)) {
		close(connection->fd);
		atomic_store_explicit(&connection->state, NT_CONNECTION_CLOSED, memory_order_release);
	}
}

void nt_connection_destroy(nt_connection_t *connection) {
	if (connection == NULL) {
		return;
	}
	nt_connection_close(connection);
	free(connection);
}
