#include "nt_jvm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	const char *java_home = getenv("JAVA_HOME");
	const char *class_path = getenv("NATIVETOMCAT_JAVA_TEST_CP");
	assert(java_home != NULL && java_home[0] != '\0');
	assert(class_path != NULL && class_path[0] != '\0');

	nt_jvm_t *jvm = NULL;
	nt_jvm_config_t config = {
		.java_home = java_home,
		.class_path = class_path,
		.bootstrap_class = "org.apache.tomcat.nativebootstrap.NativeEventDispatcherTest",
	};
	assert(nt_jvm_start(&jvm, &config) == 0);
	assert(nt_jvm_is_ready(jvm));
	assert(nt_jvm_dispatch_event(jvm, 42, 1) == 0);
	assert(nt_jvm_stop(jvm) == 0);
	nt_jvm_destroy(jvm);
	puts("nt_jvm_dispatch_test: PASS");
	return EXIT_SUCCESS;
}
