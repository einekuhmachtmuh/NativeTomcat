#include "nt_jvm.h"

#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nt_jvm {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	JavaVM *vm;
	jobject bootstrap_class;
	void *library_handle;
	char *java_home;
	char *class_path;
	char *bootstrap_class_name;
	int status;
	bool ready;
	bool stop_requested;
	bool thread_started;
};

typedef jint (*nt_jni_create_java_vm_fn)(JavaVM **, void **, void *);

typedef struct nt_jvm_thread_result {
	nt_jvm_t *jvm;
} nt_jvm_thread_result_t;

static char *nt_jvm_strdup(const char *value)
{
	if (value == NULL) {
		return NULL;
	}
	return strdup(value);
}

static const char *nt_jvm_java_home(const nt_jvm_config_t *config)
{
	if (config->java_home != NULL && config->java_home[0] != '\0') {
		return config->java_home;
	}
	return getenv("JAVA_HOME");
}

static int nt_jvm_load_library(nt_jvm_t *jvm)
{
	const char *java_home = jvm->java_home;
	char path[4096];
	int length;

	if (java_home == NULL || java_home[0] == '\0') {
		fprintf(stderr, "NativeTomcat: JAVA_HOME is required for embedded JVM startup\n");
		return -1;
	}

	length = snprintf(path, sizeof(path), "%s/lib/server/libjvm.so", java_home);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		fprintf(stderr, "NativeTomcat: JVM library path is too long\n");
		return -1;
	}

	jvm->library_handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (jvm->library_handle == NULL) {
		fprintf(stderr, "NativeTomcat: failed to load %s: %s\n", path, dlerror());
		return -1;
	}

	return 0;
}

static int nt_jvm_invoke_bootstrap(nt_jvm_t *jvm, JNIEnv *env)
{
	jclass local_class = NULL;
	jmethodID bootstrap_method = NULL;
	jobjectArray arguments = NULL;
	const char *class_name = jvm->bootstrap_class_name;
	char class_name_buffer[1024];
	size_t index;

	if (strlen(class_name) >= sizeof(class_name_buffer)) {
		return -1;
	}

	strcpy(class_name_buffer, class_name);
	for (index = 0; class_name_buffer[index] != '\0'; index++) {
		if (class_name_buffer[index] == '.') {
			class_name_buffer[index] = '/';
		}
	}

	local_class = (*env)->FindClass(env, class_name_buffer);
	if (local_class == NULL || (*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	jvm->bootstrap_class = (*env)->NewGlobalRef(env, local_class);
	if (jvm->bootstrap_class == NULL || (*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	bootstrap_method = (*env)->GetStaticMethodID(env, local_class, "bootstrap", "([Ljava/lang/String;)V");
	if (bootstrap_method == NULL || (*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	arguments = (*env)->NewObjectArray(env, 0, (*env)->FindClass(env, "java/lang/String"), NULL);
	if (arguments == NULL || (*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, local_class, bootstrap_method, arguments);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	return 0;
}

static int nt_jvm_invoke_shutdown(nt_jvm_t *jvm, JNIEnv *env)
{
	jmethodID shutdown_method;

	if (jvm->bootstrap_class == NULL) {
		return 0;
	}

	shutdown_method = (*env)->GetStaticMethodID(env, jvm->bootstrap_class, "shutdown", "()V");
	if (shutdown_method == NULL || (*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, jvm->bootstrap_class, shutdown_method);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		return -1;
	}

	return 0;
}

static void *nt_jvm_thread_main(void *argument)
{
	nt_jvm_thread_result_t *result = argument;
	nt_jvm_t *jvm = result->jvm;
	nt_jni_create_java_vm_fn create_java_vm;
	JavaVMInitArgs vm_args;
	JavaVMOption option;
	JNIEnv *env = NULL;
	jint rc;
	void *thread_status;

	free(result);

	if (nt_jvm_load_library(jvm) != 0) {
		goto fail;
	}

	*(void **)(&create_java_vm) = dlsym(jvm->library_handle, "JNI_CreateJavaVM");
	if (create_java_vm == NULL) {
		fprintf(stderr, "NativeTomcat: JNI_CreateJavaVM was not found: %s\n", dlerror());
		goto fail;
	}

	option.optionString = malloc(strlen(jvm->class_path) + 16);
	if (option.optionString == NULL) {
		goto fail;
	}
	sprintf(option.optionString, "-Djava.class.path=%s", jvm->class_path);
	option.extraInfo = NULL;

	memset(&vm_args, 0, sizeof(vm_args));
	vm_args.version = JNI_VERSION_1_8;
	vm_args.nOptions = 1;
	vm_args.options = &option;
	vm_args.ignoreUnrecognized = JNI_FALSE;

	rc = create_java_vm(&jvm->vm, (void **)&env, &vm_args);
	free(option.optionString);
	if (rc != JNI_OK) {
		fprintf(stderr, "NativeTomcat: JNI_CreateJavaVM failed with %d\n", rc);
		goto fail;
	}

	if (nt_jvm_invoke_bootstrap(jvm, env) != 0) {
		goto vm_fail;
	}

	pthread_mutex_lock(&jvm->mutex);
	jvm->ready = true;
	jvm->status = 0;
	pthread_cond_broadcast(&jvm->condition);
	while (!jvm->stop_requested) {
		pthread_cond_wait(&jvm->condition, &jvm->mutex);
	}
	pthread_mutex_unlock(&jvm->mutex);

	if (nt_jvm_invoke_shutdown(jvm, env) != 0) {
		jvm->status = -1;
	}
	goto vm_destroy;

vm_fail:
	pthread_mutex_lock(&jvm->mutex);
	jvm->ready = false;
	jvm->status = -1;
	pthread_cond_broadcast(&jvm->condition);
	pthread_mutex_unlock(&jvm->mutex);

vm_destroy:
	if (jvm->bootstrap_class != NULL) {
		(*env)->DeleteGlobalRef(env, jvm->bootstrap_class);
		jvm->bootstrap_class = NULL;
	}
	if (jvm->vm != NULL) {
		rc = (*jvm->vm)->DestroyJavaVM(jvm->vm);
		if (rc != JNI_OK && jvm->status == 0) {
			jvm->status = -1;
		}
		jvm->vm = NULL;
	}
	if (jvm->library_handle != NULL) {
		dlclose(jvm->library_handle);
		jvm->library_handle = NULL;
	}
	thread_status = (void *)(long)jvm->status;
	return thread_status;

fail:
	pthread_mutex_lock(&jvm->mutex);
	jvm->ready = false;
	jvm->status = -1;
	pthread_cond_broadcast(&jvm->condition);
	pthread_mutex_unlock(&jvm->mutex);
	thread_status = (void *)(long)-1;
	return thread_status;
}

int nt_jvm_start(nt_jvm_t **jvm, const nt_jvm_config_t *config)
{
	nt_jvm_t *instance;
	nt_jvm_thread_result_t *argument;
	int rc;

	if (jvm == NULL || config == NULL || config->class_path == NULL || config->bootstrap_class == NULL) {
		return -1;
	}

	instance = calloc(1, sizeof(*instance));
	if (instance == NULL) {
		return -1;
	}

	instance->java_home = nt_jvm_strdup(nt_jvm_java_home(config));
	instance->class_path = nt_jvm_strdup(config->class_path);
	instance->bootstrap_class_name = nt_jvm_strdup(config->bootstrap_class);
	if (instance->class_path == NULL || instance->bootstrap_class_name == NULL ||
		(config->java_home != NULL && instance->java_home == NULL)) {
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}

	rc = pthread_mutex_init(&instance->mutex, NULL);
	if (rc != 0) {
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}
	rc = pthread_cond_init(&instance->condition, NULL);
	if (rc != 0) {
		pthread_mutex_destroy(&instance->mutex);
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}

	argument = malloc(sizeof(*argument));
	if (argument == NULL) {
		pthread_cond_destroy(&instance->condition);
		pthread_mutex_destroy(&instance->mutex);
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}
	argument->jvm = instance;

	rc = pthread_create(&instance->thread, NULL, nt_jvm_thread_main, argument);
	if (rc != 0) {
		free(argument);
		pthread_cond_destroy(&instance->condition);
		pthread_mutex_destroy(&instance->mutex);
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}
	instance->thread_started = true;

	pthread_mutex_lock(&instance->mutex);
	while (!instance->ready && instance->status == 0) {
		pthread_cond_wait(&instance->condition, &instance->mutex);
	}
	rc = instance->ready ? 0 : -1;
	pthread_mutex_unlock(&instance->mutex);

	if (rc != 0) {
		pthread_join(instance->thread, NULL);
		instance->thread_started = false;
		pthread_cond_destroy(&instance->condition);
		pthread_mutex_destroy(&instance->mutex);
		free(instance->java_home);
		free(instance->class_path);
		free(instance->bootstrap_class_name);
		free(instance);
		return -1;
	}

	*jvm = instance;
	return 0;
}

int nt_jvm_stop(nt_jvm_t *jvm)
{
	int status;

	if (jvm == NULL || !jvm->thread_started) {
		return 0;
	}

	pthread_mutex_lock(&jvm->mutex);
	jvm->stop_requested = true;
	pthread_cond_broadcast(&jvm->condition);
	pthread_mutex_unlock(&jvm->mutex);

	pthread_join(jvm->thread, NULL);
	jvm->thread_started = false;
	status = jvm->status;
	return status;
}

void nt_jvm_destroy(nt_jvm_t *jvm)
{
	if (jvm == NULL) {
		return;
	}

	(void)nt_jvm_stop(jvm);
	pthread_cond_destroy(&jvm->condition);
	pthread_mutex_destroy(&jvm->mutex);
	free(jvm->java_home);
	free(jvm->class_path);
	free(jvm->bootstrap_class_name);
	free(jvm);
}

bool nt_jvm_is_ready(const nt_jvm_t *jvm)
{
	bool ready;

	if (jvm == NULL) {
		return false;
	}

	pthread_mutex_lock((pthread_mutex_t *)&jvm->mutex);
	ready = jvm->ready;
	pthread_mutex_unlock((pthread_mutex_t *)&jvm->mutex);
	return ready;
}

int nt_jvm_dispatch_event(nt_jvm_t *jvm, uint64_t connection_handle, unsigned events)
{
	JavaVMAttachArgs attach_args;
	JNIEnv *env = NULL;
	jclass bootstrap_class;
	jmethodID method;
	jint rc;
	bool attached = false;

	if (jvm == NULL || events == 0) {
		return -1;
	}

	pthread_mutex_lock(&jvm->mutex);
	if (jvm->vm == NULL || jvm->bootstrap_class == NULL || jvm->stop_requested) {
		pthread_mutex_unlock(&jvm->mutex);
		return -1;
	}
	JavaVM *vm = jvm->vm;
	jobject bootstrap_global = jvm->bootstrap_class;
	pthread_mutex_unlock(&jvm->mutex);

	rc = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_8);
	if (rc == JNI_EDETACHED) {
		memset(&attach_args, 0, sizeof(attach_args));
		attach_args.version = JNI_VERSION_1_8;
		attach_args.name = (char *)"NativeTomcat-event-loop";
		rc = (*vm)->AttachCurrentThread(vm, (void **)&env, &attach_args);
		if (rc != JNI_OK) {
			return -1;
		}
		attached = true;
	} else if (rc != JNI_OK) {
		return -1;
	}

	bootstrap_class = (*env)->NewLocalRef(env, bootstrap_global);
	if (bootstrap_class == NULL || (*env)->ExceptionCheck(env)) {
		if ((*env)->ExceptionCheck(env)) {
			(*env)->ExceptionDescribe(env);
			(*env)->ExceptionClear(env);
		}
		if (attached) {
			(*vm)->DetachCurrentThread(vm);
		}
		return -1;
	}

	method = (*env)->GetStaticMethodID(env, bootstrap_class, "dispatchNativeEvent", "(JI)V");
	if (method == NULL || (*env)->ExceptionCheck(env)) {
		if ((*env)->ExceptionCheck(env)) {
			(*env)->ExceptionDescribe(env);
			(*env)->ExceptionClear(env);
		}
		if (attached) {
			(*vm)->DetachCurrentThread(vm);
		}
		return -1;
	}

	(*env)->CallStaticVoidMethod(env, bootstrap_class, method,
			(jlong)connection_handle, (jint)events);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
		(*env)->ExceptionClear(env);
		if (attached) {
			(*vm)->DetachCurrentThread(vm);
		}
		return -1;
	}

	if (attached) {
		(*vm)->DetachCurrentThread(vm);
	}
	return 0;
}
