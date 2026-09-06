#ifndef NT_RUNTIME_H
#define NT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_runtime nt_runtime_t;

typedef struct nt_runtime_config {
	const char *bind_address;
	uint16_t port;
	int backlog;
	int max_events;
} nt_runtime_config_t;

int nt_runtime_init(nt_runtime_t **runtime, const nt_runtime_config_t *config);
int nt_runtime_run(nt_runtime_t *runtime);
void nt_runtime_stop(nt_runtime_t *runtime);
void nt_runtime_destroy(nt_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
