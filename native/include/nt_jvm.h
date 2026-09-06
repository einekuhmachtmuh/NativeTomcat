#ifndef NT_JVM_H
#define NT_JVM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nt_jvm nt_jvm_t;

typedef struct nt_jvm_config {
	const char *java_home;
	const char *class_path;
	const char *bootstrap_class;
} nt_jvm_config_t;

int nt_jvm_start(nt_jvm_t **jvm, const nt_jvm_config_t *config);
int nt_jvm_stop(nt_jvm_t *jvm);
void nt_jvm_destroy(nt_jvm_t *jvm);
bool nt_jvm_is_ready(const nt_jvm_t *jvm);
int nt_jvm_dispatch_event(nt_jvm_t *jvm, uint64_t connection_handle, unsigned events);

#ifdef __cplusplus
}
#endif

#endif
