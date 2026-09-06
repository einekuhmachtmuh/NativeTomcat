# Native runtime / connection ownership

## Scope

This document fixes the ownership contract for the first native transport layer. It does not define the later JNI or Servlet bridge.

## Ownership

- `nt_runtime_t` owns the accepted `nt_connection_t` objects.
- `nt_runtime_t` owns the listener FD, epoll FD, and wakeup FD.
- `nt_connection_t` owns its accepted socket FD.
- The connection object does not own the epoll instance and does not expose the epoll instance to Java.
- The runtime keeps each connection object alive until `nt_runtime_destroy_connections()` after the event loop has stopped. Therefore an `epoll_event.data.ptr` referring to a connection remains valid while `nt_runtime_run()` is processing events.

## Lifecycle

A connection follows:

`ACTIVE -> CLOSING -> CLOSED`

`nt_connection_close()` performs the only actual socket close and is idempotent for an already-closing or closed connection. `nt_connection_destroy()` first closes the connection and then releases the connection object.

Runtime shutdown is ordered as:

1. request event-loop stop;
2. leave the event loop;
3. close all owned connections;
4. destroy all connection objects;
5. close runtime-owned wake/listener/epoll descriptors;
6. free the runtime object.

The current implementation intentionally does **not** permit concurrent `nt_runtime_destroy()` with `nt_runtime_run()`. The caller must ensure that `nt_runtime_run()` has returned before destruction. `nt_runtime_stop()` is the cross-thread operation supported by the current runtime contract.

## Event-pointer invariant

Listener and wakeup events use addresses of stable fields inside `nt_runtime_t` as discriminator tokens. Connection events use `nt_connection_t *` in `epoll_event.data.ptr`.

A connection is never freed while `nt_runtime_run()` can still consume an epoll event containing that pointer. This avoids the immediate use-after-free hazard that would otherwise arise from storing connection pointers in epoll events.

## Threading

The atomic connection state does not make arbitrary socket operations thread-safe. The intended model is that the event-loop owner performs connection mutation and I/O. Cross-thread shutdown currently requests runtime stop through `nt_runtime_stop()` rather than closing individual connections directly.

## Current limitations

- No HTTP parser is attached to a connection yet.
- No EPOLLOUT/write-interest management is implemented yet.
- No per-connection timeout management is implemented yet.
- No Java/JNI interaction is implemented yet.
- A closed connection remains in the runtime's connection array until runtime shutdown; this is deliberate for pointer lifetime safety in this stage and can be replaced later by a deferred reclamation scheme once event-generation and ownership rules are specified.
