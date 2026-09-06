# Native connection contract

## Scope

This document defines the first NativeTomcat native connection ownership layer. It does not claim Servlet, Tomcat `SocketWrapperBase`, HTTP, TLS, or JNI compatibility yet.

## Ownership

`nt_connection_t` owns the accepted socket file descriptor for its lifetime. The descriptor is not owned by the caller after successful `nt_connection_create()`.

The connection object owns only transport state. Epoll registration remains a runtime/event-loop responsibility and is intentionally not part of the connection API.

## State

The native state machine is:

```text
ACTIVE -> CLOSING -> CLOSED
```

`nt_connection_close()` is idempotent for the connection state and closes the owned descriptor only for the successful `ACTIVE -> CLOSING` transition.

## I/O

`nt_connection_read()` and `nt_connection_write()` perform one non-blocking transport operation and return the system-call result through `ssize_t *result`.

A successful result of zero is preserved as an I/O result rather than being converted into an application-level state transition. EOF interpretation remains a higher layer concern.

Transport errors that unambiguously indicate a broken connection transition the state to `CLOSING`. The caller remains responsible for invoking `nt_connection_close()` and ultimately `nt_connection_destroy()`.

## Threading boundary

The current API does not promise that a connection object may be destroyed concurrently with an I/O operation. Such concurrency requires a higher-level owner/dispatch protocol before it can be supported safely.

The event loop owns readiness registration and dispatch. The connection object does not call into Java and does not own a Java thread or `JNIEnv`.

## Deliberate omissions

The first contract does not include:

- epoll registration or interest masks;
- native HTTP parsing;
- native buffering beyond the kernel socket;
- TLS;
- sendfile;
- Java object references;
- JNI handles;
- Tomcat `SocketWrapperBase` subclassing;
- Servlet readiness semantics.

These are separate contracts and must be verified before implementation.
