#ifndef NT_RUNTIME_H
#define NT_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_runtime nt_runtime_t;
typedef struct nt_connection nt_connection_t;

typedef enum nt_runtime_event {
	NT_RUNTIME_EVENT_READABLE = 1u << 0,
	NT_RUNTIME_EVENT_WRITABLE = 1u << 1,
	NT_RUNTIME_EVENT_PEER_READ_CLOSED = 1u << 2,
	NT_RUNTIME_EVENT_ERROR = 1u << 3
} nt_runtime_event_t;

typedef void (*nt_runtime_connection_handler_t)(
		nt_runtime_t *runtime, nt_connection_t *connection, unsigned events, void *user_data);

typedef struct nt_runtime_config {
	const char *bind_address;
	uint16_t port;
	int backlog;
	int max_events;
	nt_runtime_connection_handler_t connection_handler;
	void *connection_handler_data;
} nt_runtime_config_t;

int nt_runtime_init(nt_runtime_t **runtime, const nt_runtime_config_t *config);
int nt_runtime_get_port(const nt_runtime_t *runtime);
nt_connection_t *nt_runtime_find_connection(nt_runtime_t *runtime, uint64_t handle);
int nt_runtime_request_rearm(nt_runtime_t *runtime, uint64_t handle, bool want_write);
int nt_runtime_request_close(nt_runtime_t *runtime, uint64_t handle);
int nt_runtime_run(nt_runtime_t *runtime);
void nt_runtime_stop(nt_runtime_t *runtime);
int nt_runtime_rearm_connection(nt_runtime_t *runtime, nt_connection_t *connection, bool want_write);
void nt_runtime_destroy(nt_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
