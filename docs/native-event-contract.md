# Native connection event contract

## Scope

The current native transport layer uses Linux `epoll` with level-triggered listener/wakeup monitoring and `EPOLLONESHOT` monitoring for accepted connections.

## Ownership

- `nt_runtime_t` owns the lifetime of `nt_connection_t` objects.
- `nt_connection_t` owns its accepted socket file descriptor.
- A connection stores a non-owning pointer to its owning runtime so event rearming can reject connections belonging to another runtime.
- The Java layer does not receive the socket file descriptor.

## Connection registration

Accepted sockets are registered with:

- `EPOLLIN`
- `EPOLLRDHUP`
- `EPOLLONESHOT`

`EPOLLOUT` is added only when a caller explicitly requests write readiness through `nt_runtime_rearm_connection()`.

## Why EPOLLONESHOT is used now

The current runtime has no HTTP parser or application-consumption callback. Leaving accepted sockets in ordinary level-triggered `EPOLLIN` mode while not consuming input would repeatedly report the same readable socket and can create a busy loop.

`EPOLLONESHOT` therefore makes delivery of a readiness notification a one-shot handoff. The consumer must explicitly rearm the connection after it has processed the event. Linux documents that `EPOLLONESHOT` disables the descriptor after notification and requires `EPOLL_CTL_MOD` to rearm it.

## Current event behavior

- Listener readiness: accept all available connections until `EAGAIN`/`EWOULDBLOCK`.
- Wake readiness: drain the eventfd.
- Connection error/hangup/read-half-close: transition the connection toward closed state.
- Connection readable/writable without an error: the runtime currently does not consume application data. This is deliberate; HTTP parsing and buffer ownership are not implemented yet.
- The caller may rearm a live connection with read interest and, when required by pending output, write interest.

## Important limitation

`nt_runtime_rearm_connection()` currently assumes the caller obeys the event-loop ownership model. `nt_runtime_destroy()` must not run concurrently with `nt_runtime_run()`. Cross-thread event requests and their wakeup/serialization mechanism will be defined before JNI exposes this API.

## Next layer

The next native layer should define the connection input/output buffer contract before implementing HTTP parsing. In particular, it must specify:

1. whether a read buffer is owned by C or borrowed by Java;
2. how partial reads and partial writes are represented;
3. when `EPOLLOUT` is armed/disarmed;
4. how a Java consumer requests rearming without racing the event-loop owner;
5. how close/error events are propagated exactly once.
