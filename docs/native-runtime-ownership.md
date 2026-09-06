# Native runtime / connection ownership

## 1. Scope and status

This document defines ownership and lifetime for the current native transport runtime. It is an implementation contract for `nt_runtime_t` and `nt_connection_t`; it does not by itself establish Tomcat or Servlet compatibility.

The current implementation includes Linux `epoll`, accepted-connection ownership, `EPOLLONESHOT`, a native event callback, and a JNI dispatch path. The document therefore distinguishes **implemented**, **source-verified**, **tested**, and **deferred** behavior rather than describing the runtime as if later integration already existed.

## 2. Ownership model

`nt_runtime_t` owns:

- the listener FD;
- the epoll FD;
- the wakeup/eventfd mechanism;
- the set of accepted `nt_connection_t` objects;
- epoll registration and rearm decisions;
- the native event-loop execution context.

`nt_connection_t` owns:

- its accepted socket FD;
- transport state associated with that FD;
- its native lifecycle state.

The connection does not own the epoll instance and does not expose the epoll FD or socket FD to Java.

The current JNI boundary passes an opaque native connection handle to Java. The handle is an identifier for native ownership; it is not ownership transfer and must not be interpreted as a file descriptor.

## 3. Lifetime invariant

Connection pointers are stored in `epoll_event.data.ptr`. Therefore a connection object must remain allocated for as long as the event loop can consume an epoll event containing that pointer.

The current implementation deliberately retains connection objects in the runtime-owned collection until the event loop has stopped and the runtime destruction phase begins. This is a conservative lifetime rule, not the final reclamation architecture.

Required invariant:

```text
epoll may still deliver/consume pointer
        => nt_connection_t must still exist
```

Any future removal of this retention rule requires an explicit generation/deferred-reclamation protocol and tests for stale epoll events.

## 4. Connection lifecycle

The transport state machine is:

```text
ACTIVE -> CLOSING -> CLOSED
```

`nt_connection_close()` is the idempotent operation that performs the actual socket close for the active-to-closing transition. `nt_connection_destroy()` closes if necessary and then releases the connection object.

A Java callback returning is **not** a valid signal that a native connection can be destroyed. Java work may have been queued to a Tomcat Executor, and later transport/protocol state may still refer to the logical connection.

## 5. Event-loop ownership

The event-loop thread is the owner of:

- readiness notifications;
- epoll interest/rearm operations;
- native connection mutation required by event processing;
- native non-blocking transport I/O performed as part of event handling.

The atomic connection state does not make arbitrary connection operations safe from unrelated threads.

Cross-thread shutdown currently uses `nt_runtime_stop()`. It does not authorize another thread to destroy a connection or mutate epoll registration directly.

## 6. EPOLLONESHOT ownership

Accepted sockets use `EPOLLONESHOT`. After an event is delivered, the registration is disabled until an explicit rearm.

The ownership rule is therefore:

```text
kernel readiness
    -> native event-loop callback
    -> work is dispatched/consumed
    -> next interest set is determined
    -> native event-loop owner rearms exactly once
```

The current code has a transitional mismatch here: the JNI dispatch currently queues Java work and the native callback can rearm before that Java work has consumed the connection. This is **not** the final ownership contract. The code must be corrected before native transport integration relies on `EPOLLONESHOT` as a back-pressure mechanism.

In particular, `Java task submitted` must not be treated as `transport consumed`.

## 7. JNI boundary

The native event callback can dispatch an opaque connection handle and event mask through JNI to `NativeTomcatBootstrap.dispatchNativeEvent()`.

The current Java path is:

```text
native epoll thread
    -> JNI
    -> NativeTomcatBootstrap
    -> NativeEventDispatcher
    -> Tomcat connector Executor
```

This establishes an event-dispatch boundary only. It does not yet establish native read/write access from Java, a `SocketWrapperBase` mapping, or HTTP processing.

Consequently the current implementation must not claim that the JNI callback has consumed socket data or completed Tomcat socket processing.

## 8. Shutdown ordering

The current native runtime requires `nt_runtime_run()` to return before `nt_runtime_destroy()` is called.

The native shutdown sequence is:

1. request event-loop stop;
2. leave `nt_runtime_run()`;
3. close owned connections;
4. destroy connection objects;
5. close wake/listener/epoll descriptors;
6. free the runtime object.

Once Java transport references are introduced, this sequence must be extended so that no Java task can access a connection after native destruction. That final cross-runtime destruction protocol is deferred until the JNI transport contract exists.

## 9. Current status

### Implemented

- listener, epoll and wakeup ownership;
- accepted-connection ownership;
- connection state machine;
- pointer-lifetime retention through event-loop shutdown;
- native event callback;
- JNI event dispatch boundary;
- `EPOLLONESHOT` registration and explicit rearm API.

### Tested

The native runtime test currently verifies the implemented runtime behavior. The JNI dispatch test verifies the JNI event-dispatch path.

### Not yet verified end-to-end

- Java-triggered native read/write operations;
- rearm after actual Tomcat transport consumption;
- native handle to `SocketWrapperBase` lifetime mapping;
- connector shutdown with live native connections;
- race-free destruction with queued Java tasks;
- HTTP request/response through the native transport.

### Deferred

- per-connection timeout ownership;
- deferred connection reclamation/generation scheme;
- TLS ownership;
- sendfile and vectored I/O;
- final JNI/global-reference destruction protocol.

## 10. Contract boundary

This document is authoritative only for NativeTomcat's native ownership design. Tomcat transport semantics must be established from the pinned Tomcat 11.0.25 source, and application-visible behavior from Servlet 6.1. NGINX may be used to cross-check native event architecture, but it does not define Tomcat or Servlet semantics.
