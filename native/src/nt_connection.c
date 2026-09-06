#include "nt_connection.h"

#include <errno.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <unistd.h>

struct nt_connection {
	int fd;
	atomic_int state;
};

int nt_connection_create(nt_connection_t **connection, int fd) {
	if (connection == NULL || fd < 0) {
		errno = EINVAL;
		return -1;
	}

	nt_connection_t *value = malloc(sizeof(*value));
	if (value == NULL) {
		return -1;
	}

	value->fd = fd;
	atomic_init(&value->state, NT_CONNECTION_ACTIVE);
	*connection = value;
	return 0;
}

int nt_connection_get_fd(const nt_connection_t *connection) {
	if (connection == NULL) {
		errno = EINVAL;
		return -1;
	}
	return connection->fd;
}

nt_connection_state_t nt_connection_get_state(const nt_connection_t *connection) {
	if (connection == NULL) {
		return NT_CONNECTION_CLOSED;
	}
	return (nt_connection_state_t)atomic_load_explicit(&connection->state, memory_order_acquire);
}

int nt_connection_read(nt_connection_t *connection, void *buffer, size_t length, ssize_t *result) {
	if (connection == NULL || result == NULL || (buffer == NULL && length != 0)) {
		errno = EINVAL;
		return -1;
	}
	if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
		errno = EBADF;
		return -1;
	}

	ssize_t value = recv(connection->fd, buffer, length, 0);
	if (value >= 0) {
		*result = value;
		return 0;
	}
	if (errno == EBADF || errno == ECONNRESET || errno == ENOTCONN) {
		atomic_store_explicit(&connection->state, NT_CONNECTION_CLOSING, memory_order_release);
	}
	return -1;
}

int nt_connection_write(nt_connection_t *connection, const void *buffer, size_t length, ssize_t *result) {
	if (connection == NULL || result == NULL || (buffer == NULL && length != 0)) {
		errno = EINVAL;
		return -1;
	}
	if (nt_connection_get_state(connection) != NT_CONNECTION_ACTIVE) {
		errno = EBADF;
		return -1;
	}

	ssize_t value = send(connection->fd, buffer, length, MSG_NOSIGNAL);
	if (value >= 0) {
		*result = value;
		return 0;
	}
	if (errno == EBADF || errno == ECONNRESET || errno == EPIPE || errno == ENOTCONN) {
		atomic_store_explicit(&connection->state, NT_CONNECTION_CLOSING, memory_order_release);
	}
	return -1;
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
