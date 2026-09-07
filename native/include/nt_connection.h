#ifndef NT_CONNECTION_H
#define NT_CONNECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_connection nt_connection_t;
typedef struct nt_runtime nt_runtime_t;

typedef enum nt_connection_state {
	NT_CONNECTION_ACTIVE = 0,
	NT_CONNECTION_CLOSING = 1,
	NT_CONNECTION_CLOSED = 2
} nt_connection_state_t;

typedef enum nt_connection_io_result {
	NT_CONNECTION_IO_OK = 0,
	NT_CONNECTION_IO_WOULD_BLOCK = 1,
	NT_CONNECTION_IO_EOF = 2,
	NT_CONNECTION_IO_ERROR = -1
} nt_connection_io_result_t;

int nt_connection_create(nt_connection_t **connection, nt_runtime_t *runtime, int fd);
uint64_t nt_connection_get_handle(const nt_connection_t *connection);
int nt_connection_get_fd(const nt_connection_t *connection);
nt_runtime_t *nt_connection_get_runtime(const nt_connection_t *connection);
nt_connection_state_t nt_connection_get_state(const nt_connection_t *connection);
bool nt_connection_peer_read_closed(const nt_connection_t *connection);
void nt_connection_mark_peer_read_closed(nt_connection_t *connection);
nt_connection_io_result_t nt_connection_read(nt_connection_t *connection, void *buffer, size_t length, ssize_t *result);
nt_connection_io_result_t nt_connection_write(nt_connection_t *connection, const void *buffer, size_t length, ssize_t *result);
void nt_connection_close(nt_connection_t *connection);
void nt_connection_destroy(nt_connection_t *connection);

#ifdef __cplusplus
}
#endif

#endif
