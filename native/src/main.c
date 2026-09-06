#include "nt_jvm.h"
#include "nt_runtime.h"
#include "nt_connection.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>


static void native_connection_handler(nt_runtime_t *runtime, nt_connection_t *connection, unsigned events, void *user_data)
{
	nt_jvm_t *jvm = user_data;
	if (nt_jvm_dispatch_event(jvm, (uint64_t)(uintptr_t)connection, events) != 0) {
		nt_connection_close(connection);
		return;
	}
	if (nt_connection_get_state(connection) == NT_CONNECTION_ACTIVE &&
			nt_runtime_rearm_connection(runtime, connection, false) != 0) {
		nt_connection_close(connection);
	}
}

int main(void)
{
	const char *class_path = getenv("NATIVETOMCAT_JAVA_CP");
	nt_jvm_config_t config;
	nt_jvm_t *jvm = NULL;
	int status;

	if (class_path == NULL || class_path[0] == '\0') {
		fprintf(stderr, "NativeTomcat: NATIVETOMCAT_JAVA_CP is required\n");
		return EXIT_FAILURE;
	}

	config.java_home = getenv("JAVA_HOME");
	config.class_path = class_path;
	config.bootstrap_class = "org.apache.tomcat.nativebootstrap.NativeTomcatBootstrap";

	status = nt_jvm_start(&jvm, &config);
	if (status != 0) {
		fprintf(stderr, "NativeTomcat: embedded JVM bootstrap failed\n");
		return EXIT_FAILURE;
	}

	if (!nt_jvm_is_ready(jvm)) {
		fprintf(stderr, "NativeTomcat: JVM reported not-ready after startup\n");
		nt_jvm_destroy(jvm);
		return EXIT_FAILURE;
	}

	const char *native_port_value = getenv("NATIVETOMCAT_NATIVE_LISTEN_PORT");
	if (native_port_value != NULL && native_port_value[0] != '\0') {
		char *end = NULL;
		long port = strtol(native_port_value, &end, 10);
		if (*end != '\0' || port < 0 || port > 65535) {
			fprintf(stderr, "NativeTomcat: invalid NATIVETOMCAT_NATIVE_LISTEN_PORT\n");
			nt_jvm_stop(jvm);
			nt_jvm_destroy(jvm);
			return EXIT_FAILURE;
		}

		nt_runtime_t *runtime = NULL;
		nt_runtime_config_t runtime_config = {
			.bind_address = "127.0.0.1",
			.port = (uint16_t) port,
			.backlog = 128,
			.max_events = 256,
			.connection_handler = native_connection_handler,
			.connection_handler_data = jvm,
		};
		status = nt_runtime_init(&runtime, &runtime_config);
		if (status != 0) {
			fprintf(stderr, "NativeTomcat: native runtime initialization failed: %s\n", strerror(errno));
			nt_jvm_stop(jvm);
			nt_jvm_destroy(jvm);
			return EXIT_FAILURE;
		}
		status = nt_runtime_run(runtime);
		nt_runtime_destroy(runtime);
	}

	status = nt_jvm_stop(jvm);
	nt_jvm_destroy(jvm);
	if (status != 0) {
		fprintf(stderr, "NativeTomcat: embedded JVM shutdown failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
