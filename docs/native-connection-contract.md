# Native connection contract

## 1. Scope and status

This document defines the native transport object `nt_connection_t`: descriptor ownership, state, non-blocking I/O and thread boundary. It does not establish Tomcat `SocketWrapperBase`, Servlet, HTTP, TLS, or final JNI semantics.

The current implementation already provides one-shot native read/write primitives and connection lifecycle management. Higher layers must still define how those primitives are scheduled and how their results become Tomcat transport state.

## 2. Ownership

`nt_connection_t` owns the accepted socket file descriptor after successful creation and until close.

The epoll instance and interest mask are owned by `nt_runtime_t`; they are deliberately outside the connection object API.

The Java layer receives, at most, an opaque native handle. It does not receive ownership of the descriptor and must never close the descriptor directly.

## 3. Lifecycle

The native state machine is:

```text
ACTIVE -> CLOSING -> CLOSED
```

`nt_connection_close()` is idempotent at the connection-state level. `nt_connection_destroy()` is a final memory-lifetime operation and must not run while another operation can still access the connection.

The current runtime retains connection objects until event-loop shutdown so that `epoll_event.data.ptr` cannot become a dangling pointer during the active loop.

## 4. I/O contract

`nt_connection_read()` and `nt_connection_write()` perform one non-blocking transport operation and return the underlying operation result through the native API.

The native layer distinguishes successful byte transfer from would-block and terminal/error conditions. EOF interpretation at the HTTP/Tomcat layer remains separate from the low-level system-call result.

The current implementation retries `EINTR` inside the native operation. The final JNI ABI must preserve enough information to distinguish interruption from `EAGAIN`/`EWOULDBLOCK` if the higher layer needs that distinction.

## 5. Event-loop relationship

The connection object does not own readiness registration. The runtime owns:

- epoll registration;
- event dispatch;
- `EPOLLONESHOT` rearm;
- wakeup coordination;
- the event-loop execution context.

Therefore a successful `nt_connection_read()` does not itself rearm epoll, and a Java thread must not bypass the runtime owner to mutate registration.

## 6. Threading

The current API does not guarantee that connection destruction may race with I/O. The event-loop owner is the default authority for connection mutation and I/O.

Cross-thread operations require an explicit dispatch protocol. `nt_runtime_stop()` is currently the supported cross-thread shutdown request; it does not make arbitrary connection operations thread-safe.

Once Java transport operations are introduced, the project must specify whether the native I/O occurs on the event-loop thread or is marshalled there, and how completion is reported back to Java.

## 7. Native/Java boundary

The current JNI event path carries an opaque connection handle and an event mask. It does not yet provide the complete transport API needed by `NativeSocketWrapper`.

The next JNI transport surface is expected to cover, at minimum:

- non-blocking read into a bounded Java `ByteBuffer` window;
- non-blocking write from a bounded Java `ByteBuffer` window;
- connection close request;
- event-interest/rearm request through the native owner;
- unambiguous result/status mapping.

The exact signatures must be derived from the actual `SocketWrapperBase` methods and the chosen event-loop ownership model; they must not be frozen from this document alone.

## 8. Error and EOF semantics

A peer read-half-close, orderly EOF, transport error and local close are distinct transport facts. They may lead to the same final connection state, but the Tomcat protocol layer must receive enough information to decide whether pending response output, keep-alive, upgrade or error handling remains possible.

The native layer must not unilaterally equate `EPOLLRDHUP` with immediate destruction.

## 9. Explicit non-claims

This contract does not claim:

- one read equals one HTTP request;
- one write equals complete peer delivery;
- native readiness equals Servlet readiness;
- native connection lifetime equals Tomcat wrapper lifetime;
- JNI event dispatch equals Tomcat `processSocket()` execution;
- current connection retention is the final memory-reclamation scheme.

## 10. Required verification before ABI freeze

Before freezing the JNI transport ABI, executable tests must cover:

- partial read/write;
- `EAGAIN`/`EWOULDBLOCK`;
- `EINTR`;
- EOF and peer half-close;
- reset/error;
- close during pending output;
- exactly-once event/rearm behavior;
- connection destruction only after all Java/native users have released the object;
- direct-buffer and fallback-buffer paths where applicable.
