#include "nt_connection.h"
#include "nt_runtime.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct test_context {
	atomic_int readable_events;
	atomic_int peer_eof_events;
	atomic_bool callback_error;
	atomic_uint_fast64_t first_handle;
};

static void test_connection_handler(nt_runtime_t *runtime, nt_connection_t *connection,
		unsigned events, void *user_data) {
	struct test_context *context = user_data;
	char buffer[256];
	uint64_t handle = nt_connection_get_handle(connection);

	if (handle == 0 || nt_runtime_find_connection(runtime, handle) != connection) {
		atomic_store(&context->callback_error, true);
		nt_connection_close(connection);
		nt_runtime_stop(runtime);
		return;
	}
	uint64_t expected = 0;
	atomic_compare_exchange_strong(&context->first_handle, &expected, handle);

	if ((events & NT_RUNTIME_EVENT_ERROR) != 0) {
		atomic_store(&context->callback_error, true);
		nt_connection_close(connection);
		nt_runtime_stop(runtime);
		return;
	}

	if ((events & NT_RUNTIME_EVENT_READABLE) != 0) {
		atomic_fetch_add(&context->readable_events, 1);
		for (;;) {
			ssize_t nread = 0;
			nt_connection_io_result_t result = nt_connection_read(connection, buffer, sizeof(buffer), &nread);
			if (result == NT_CONNECTION_IO_WOULD_BLOCK) {
				break;
			}
			if (result == NT_CONNECTION_IO_EOF) {
				atomic_fetch_add(&context->peer_eof_events, 1);
				nt_connection_close(connection);
				nt_runtime_stop(runtime);
				return;
			}
			if (result != NT_CONNECTION_IO_OK) {
				atomic_store(&context->callback_error, true);
				nt_connection_close(connection);
				nt_runtime_stop(runtime);
				return;
			}

			ssize_t offset = 0;
			while (offset < nread) {
				ssize_t nwritten = 0;
				result = nt_connection_write(connection, buffer + offset,
						(size_t) (nread - offset), &nwritten);
				if (result == NT_CONNECTION_IO_OK && nwritten > 0) {
					offset += nwritten;
					continue;
				}
				if (result == NT_CONNECTION_IO_WOULD_BLOCK) {
					atomic_store(&context->callback_error, true);
					nt_connection_close(connection);
					nt_runtime_stop(runtime);
					return;
				}
				atomic_store(&context->callback_error, true);
				nt_connection_close(connection);
				nt_runtime_stop(runtime);
				return;
			}
		}
	}
}

static void *runtime_thread(void *arg) {
	nt_runtime_t *runtime = arg;
	int result = nt_runtime_run(runtime);
	return (void *) (long) result;
}

static int connect_loopback(int port) {
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	assert(fd >= 0);

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t) port);
	assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);

	for (int attempt = 0; attempt < 100; ++attempt) {
		if (connect(fd, (struct sockaddr *) &address, sizeof(address)) == 0) {
			return fd;
		}
		if (errno != ECONNREFUSED && errno != EINTR) {
			break;
		}
		usleep(1000);
	}

	close(fd);
	return -1;
}

static void send_and_expect_echo(int fd, const char *message) {
	size_t length = strlen(message);
	assert(send(fd, message, length, 0) == (ssize_t) length);

	char buffer[256];
	ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
	assert(received == (ssize_t) length);
	assert(memcmp(buffer, message, length) == 0);
}

static void wait_for_closed(nt_connection_t *connection) {
	for (int attempt = 0; attempt < 100; ++attempt) {
		if (nt_connection_get_state(connection) == NT_CONNECTION_CLOSED) {
			return;
		}
		usleep(1000);
	}
	assert(nt_connection_get_state(connection) == NT_CONNECTION_CLOSED);
}

int main(void) {
	struct test_context context;
	atomic_init(&context.readable_events, 0);
	atomic_init(&context.peer_eof_events, 0);
	atomic_init(&context.callback_error, false);
	atomic_init(&context.first_handle, 0);

	nt_runtime_t *runtime = NULL;
	nt_runtime_config_t config = {
		.bind_address = "127.0.0.1",
		.port = 0,
		.backlog = 16,
		.max_events = 16,
		.connection_handler = test_connection_handler,
		.connection_handler_data = &context,
	};
	assert(nt_runtime_init(&runtime, &config) == 0);

	int port = nt_runtime_get_port(runtime);
	assert(port > 0);

	pthread_t thread;
	assert(pthread_create(&thread, NULL, runtime_thread, runtime) == 0);

	int client = connect_loopback(port);
	assert(client >= 0);

	send_and_expect_echo(client, "first");
	uint64_t handle = atomic_load(&context.first_handle);
	assert(handle != 0);
	assert(nt_runtime_request_rearm(runtime, handle, false) == 0);

	send_and_expect_echo(client, "second");
	assert(atomic_load(&context.readable_events) >= 2);

	/* Multiple rearm requests for one handle must collapse to the final state,
	 * and a later close must dominate every rearm in the same queue batch. */
	assert(nt_runtime_request_rearm(runtime, handle, false) == 0);
	assert(nt_runtime_request_rearm(runtime, handle, true) == 0);
	assert(nt_runtime_request_rearm(runtime, handle, false) == 0);
	assert(nt_runtime_request_close(runtime, handle) == 0);

	nt_connection_t *connection = nt_runtime_find_connection(runtime, handle);
	assert(connection != NULL);
	wait_for_closed(connection);
	assert(nt_runtime_find_connection(runtime, handle) == NULL);

	/* A command targeting a closed handle may be submitted asynchronously but
	 * must be harmless when the event loop resolves the handle. */
	assert(nt_runtime_request_rearm(runtime, handle, false) == 0);
	assert(nt_runtime_request_close(runtime, handle) == 0);

	nt_runtime_stop(runtime);
	assert(pthread_join(thread, NULL) == 0);
	assert(!atomic_load(&context.callback_error));
	assert(atomic_load(&context.peer_eof_events) == 0);

	errno = 0;
	assert(nt_runtime_request_rearm(runtime, handle, false) == -1);
	assert(errno == ECANCELED);
	errno = 0;
	assert(nt_runtime_request_close(runtime, handle) == -1);
	assert(errno == ECANCELED);

	close(client);
	nt_runtime_destroy(runtime);
	puts("nt_runtime_test: PASS");
	return 0;
}
