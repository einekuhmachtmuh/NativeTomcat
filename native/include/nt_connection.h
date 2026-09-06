#ifndef NT_CONNECTION_H
#define NT_CONNECTION_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_connection nt_connection_t;

typedef enum nt_connection_state {
	NT_CONNECTION_ACTIVE = 0,
	NT_CONNECTION_CLOSING = 1,
	NT_CONNECTION_CLOSED = 2
} nt_connection_state_t;

int nt_connection_create(nt_connection_t **connection, int fd);
int nt_connection_get_fd(const nt_connection_t *connection);
nt_connection_state_t nt_connection_get_state(const nt_connection_t *connection);
int nt_connection_read(nt_connection_t *connection, void *buffer, size_t length, ssize_t *result);
int nt_connection_write(nt_connection_t *connection, const void *buffer, size_t length, ssize_t *result);
void nt_connection_close(nt_connection_t *connection);
void nt_connection_destroy(nt_connection_t *connection);

#ifdef __cplusplus
}

#endif

#endif
