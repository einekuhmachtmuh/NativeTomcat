#include "nt_native_transport.h"
#include "nt_connection.h"

#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_rwlock_t nt_native_transport_lock = PTHREAD_RWLOCK_INITIALIZER;
static nt_runtime_t *nt_native_transport_runtime;

int nt_native_transport_bind_runtime(nt_runtime_t *runtime)
{
	int rc;

	rc = pthread_rwlock_wrlock(&nt_native_transport_lock);
	if (rc != 0) {
		errno = rc;
		return -1;
	}

	nt_native_transport_runtime = runtime;

	rc = pthread_rwlock_unlock(&nt_native_transport_lock);
	if (rc != 0) {
		errno = rc;
		return -1;
	}
	return 0;
}

static nt_runtime_t *nt_native_transport_get_runtime(void)
{
	return nt_native_transport_runtime;
}

static void nt_native_transport_throw_io(JNIEnv *env, const char *message)
{
	jclass exception_class = (*env)->FindClass(env, "java/io/IOException");
	if (exception_class != NULL) {
		(*env)->ThrowNew(env, exception_class, message);
	}
}

static int nt_native_transport_get_buffer(JNIEnv *env, jobject buffer, void **address, jlong *capacity)
{
	void *direct_address;
	jlong direct_capacity;

	if (buffer == NULL) {
		nt_native_transport_throw_io(env, "native transport requires a ByteBuffer");
		return -1;
	}

	direct_address = (*env)->GetDirectBufferAddress(env, buffer);
	direct_capacity = (*env)->GetDirectBufferCapacity(env, buffer);
	if (direct_address == NULL || direct_capacity < 0) {
		nt_native_transport_throw_io(env, "native transport requires a direct ByteBuffer");
		return -1;
	}

	*address = direct_address;
	*capacity = direct_capacity;
	return 0;
}

static int nt_native_transport_get_position_limit(JNIEnv *env, jobject buffer, jlong *position, jlong *limit)
{
	jclass buffer_class = (*env)->GetObjectClass(env, buffer);
	jmethodID position_method;
	jmethodID limit_method;

	if (buffer_class == NULL) {
		return -1;
	}

	position_method = (*env)->GetMethodID(env, buffer_class, "position", "()I");
	limit_method = (*env)->GetMethodID(env, buffer_class, "limit", "()I");
	if (position_method == NULL || limit_method == NULL) {
		return -1;
	}

	*position = (*env)->CallIntMethod(env, buffer, position_method);
	*limit = (*env)->CallIntMethod(env, buffer, limit_method);
	if ((*env)->ExceptionCheck(env)) {
		return -1;
	}
	return 0;
}

static int nt_native_transport_advance_position(JNIEnv *env, jobject buffer, jint position)
{
	jclass buffer_class = (*env)->GetObjectClass(env, buffer);
	jmethodID position_method;

	if (buffer_class == NULL) {
		return -1;
	}
	position_method = (*env)->GetMethodID(env, buffer_class, "position", "(I)Ljava/nio/Buffer;");
	if (position_method == NULL) {
		return -1;
	}
	(void)(*env)->CallObjectMethod(env, buffer, position_method, position);
	return (*env)->ExceptionCheck(env) ? -1 : 0;
}

static int nt_native_transport_io_result(JNIEnv *env, nt_connection_io_result_t result, ssize_t count,
		const char *operation)
{
	if (result == NT_CONNECTION_IO_OK) {
		return (int)count;
	}
	if (result == NT_CONNECTION_IO_WOULD_BLOCK) {
		return 0;
	}
	if (result == NT_CONNECTION_IO_EOF) {
		return -1;
	}

	char message[128];
	snprintf(message, sizeof(message), "native %s failed: %s", operation, strerror(errno));
	nt_native_transport_throw_io(env, message);
	return -2;
}

JNIEXPORT jint JNICALL Java_org_apache_tomcat_util_net_NativeTransport_read(JNIEnv *env, jclass clazz,
		jlong handle, jobject buffer)
{
	nt_runtime_t *runtime;
	nt_connection_t *connection;
	void *address;
	jlong capacity;
	jlong position;
	jlong limit;
	ssize_t count = 0;
	nt_connection_io_result_t result;
	int rc;
	(void)clazz;

	rc = pthread_rwlock_rdlock(&nt_native_transport_lock);
	if (rc != 0) {
		nt_native_transport_throw_io(env, "native transport runtime lock failed");
		return -1;
	}
	runtime = nt_native_transport_get_runtime();
	if (runtime == NULL) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "native transport runtime is not bound");
		return -1;
	}
	connection = nt_runtime_find_connection(runtime, (uint64_t)handle);
	if (connection == NULL) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "native connection handle is not active");
		return -1;
	}
	if (nt_native_transport_get_buffer(env, buffer, &address, &capacity) != 0 ||
		nt_native_transport_get_position_limit(env, buffer, &position, &limit) != 0) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		return -1;
	}
	if (position < 0 || limit < position || limit > capacity) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "invalid ByteBuffer bounds");
		return -1;
	}

	result = nt_connection_read(connection, (char *)address + position, (size_t)(limit - position), &count);
	pthread_rwlock_unlock(&nt_native_transport_lock);
	if (result == NT_CONNECTION_IO_OK && count > 0) {
		if (nt_native_transport_advance_position(env, buffer, (jint)(position + count)) != 0) {
			return -1;
		}
	}
	return nt_native_transport_io_result(env, result, count, "read");
}

JNIEXPORT jint JNICALL Java_org_apache_tomcat_util_net_NativeTransport_write(JNIEnv *env, jclass clazz,
		jlong handle, jobject buffer)
{
	nt_runtime_t *runtime;
	nt_connection_t *connection;
	void *address;
	jlong capacity;
	jlong position;
	jlong limit;
	ssize_t count = 0;
	nt_connection_io_result_t result;
	int rc;
	(void)clazz;

	rc = pthread_rwlock_rdlock(&nt_native_transport_lock);
	if (rc != 0) {
		nt_native_transport_throw_io(env, "native transport runtime lock failed");
		return -1;
	}
	runtime = nt_native_transport_get_runtime();
	if (runtime == NULL) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "native transport runtime is not bound");
		return -1;
	}
	connection = nt_runtime_find_connection(runtime, (uint64_t)handle);
	if (connection == NULL) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "native connection handle is not active");
		return -1;
	}
	if (nt_native_transport_get_buffer(env, buffer, &address, &capacity) != 0 ||
		nt_native_transport_get_position_limit(env, buffer, &position, &limit) != 0) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		return -1;
	}
	if (position < 0 || limit < position || limit > capacity) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "invalid ByteBuffer bounds");
		return -1;
	}

	result = nt_connection_write(connection, (const char *)address + position, (size_t)(limit - position), &count);
	pthread_rwlock_unlock(&nt_native_transport_lock);
	if (result == NT_CONNECTION_IO_OK && count > 0) {
		if (nt_native_transport_advance_position(env, buffer, (jint)(position + count)) != 0) {
			return -1;
		}
	}
	return nt_native_transport_io_result(env, result, count, "write");
}

JNIEXPORT void JNICALL Java_org_apache_tomcat_util_net_NativeTransport_rearm(JNIEnv *env, jclass clazz,
		jlong handle, jboolean want_write)
{
	nt_runtime_t *runtime;
	int rc;
	(void)clazz;

	rc = pthread_rwlock_rdlock(&nt_native_transport_lock);
	if (rc != 0) {
		nt_native_transport_throw_io(env, "native transport runtime lock failed");
		return;
	}
	runtime = nt_native_transport_get_runtime();
	if (runtime == NULL) {
		pthread_rwlock_unlock(&nt_native_transport_lock);
		nt_native_transport_throw_io(env, "native transport runtime is not bound");
		return;
	}
	rc = nt_runtime_request_rearm(runtime, (uint64_t)handle, want_write == JNI_TRUE);
	pthread_rwlock_unlock(&nt_native_transport_lock);
	if (rc != 0) {
		char message[128];
		snprintf(message, sizeof(message), "native rearm failed: %s", strerror(errno));
		nt_native_transport_throw_io(env, message);
	}
}

JNIEXPORT void JNICALL Java_org_apache_tomcat_util_net_NativeTransport_close(JNIEnv *env, jclass clazz,
		jlong handle)
{
	nt_runtime_t *runtime;
	int rc;
	(void)clazz;

	rc = pthread_rwlock_rdlock(&nt_native_transport_lock);
	if (rc != 0) {
		return;
	}
	runtime = nt_native_transport_get_runtime();
	if (runtime != NULL) {
		(void)nt_runtime_request_close(runtime, (uint64_t)handle);
	}
	pthread_rwlock_unlock(&nt_native_transport_lock);
	(void)env;
}
