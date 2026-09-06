#include "nt_jvm.h"

#include <stdio.h>
#include <stdlib.h>

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

	status = nt_jvm_stop(jvm);
	nt_jvm_destroy(jvm);
	if (status != 0) {
		fprintf(stderr, "NativeTomcat: embedded JVM shutdown failed\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
