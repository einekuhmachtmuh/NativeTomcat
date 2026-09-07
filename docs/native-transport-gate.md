# Native transport gate

This document records the source-verified contract for the next NativeTomcat integration step. It is an implementation gate, not a claim that the transport bridge is complete.

## Current status

- `SocketWrapperBase` is the pinned Tomcat 11.0.25 source at `cbe6e15ee81e2fc6232954292a80cca5d1e84009`; the repository copy is format-normalized and its content has been source-audited.
- `NativeSocketWrapper` is still a surface checkpoint. Its real `read`, `doWrite`, `doClose`, and interest methods are not yet implemented.
- `NativeTransport.java` defines the intended Java/native transport boundary, but no JNI implementation is registered yet.
- Native connection handles are stable `uint64_t` values and are looked up through the native runtime registry.
- Native epoll uses `EPOLLIN | EPOLLRDHUP | EPOLLONESHOT`, with optional `EPOLLOUT`. `EPOLLONESHOT` is a NativeTomcat ownership choice; it is not a claim that NGINX uses the same epoll mode.

## Tomcat contract

The pinned `SocketWrapperBase` contract requires transport implementations to provide:

- `read(boolean, byte[])` and `read(boolean, ByteBuffer)`;
- `isReadyForRead()`;
- `doWrite(boolean, ByteBuffer)`;
- `flushNonBlocking()`;
- `registerReadInterest()` and `registerWriteInterest()`;
- `doClose()`.

`SocketWrapperBase` itself owns the higher-level read/write buffer protocol. In particular, blocking and non-blocking writes use the socket write buffer and the non-blocking write buffer before calling `doWrite()`. A `NativeSocketWrapper` must therefore not bypass those buffers merely to reach `send(2)`.

The pinned NIO implementation also distinguishes Java/Tomcat read semantics from kernel readiness. A non-blocking read can return no data; a blocking read waits for a future readiness event. EOF is distinct from `WOULD_BLOCK` and must reach Tomcat close/read semantics rather than being treated as ordinary zero-byte data.

## Native ownership contract

The native event loop is the sole owner of:

- epoll registration;
- readiness interest;
- `EPOLLONESHOT` rearm;
- native connection close/destroy;
- native connection registry lifetime.

A Java worker must not call `epoll_ctl()` directly. Therefore `NativeTransport.rearm()` must not be a direct worker-thread `epoll_ctl()` wrapper. If Java processing changes interest, it must submit a command to the native event-loop owner, which performs the actual rearm.

Likewise, Java-side `doClose()` must request native close through the ownership boundary rather than freeing a connection object directly. The native registry must retain the connection object until no event-loop or Java-dispatch path can reference it.

## Required event cycle

The target cycle is:

```text
kernel readiness
    -> native epoll event
    -> stable handle
    -> Java dispatcher / SocketProcessor
    -> SocketWrapperBase transport consumption
    -> Java reports resulting interest/close state
    -> native event-loop command queue
    -> exactly one rearm OR close
```

`Java task submitted` is not `transport consumed`. The native event loop must not rearm immediately after scheduling Java work, because the Java processor may still be consuming data or may close the wrapper.

## Command-queue gate

The next implementation gate is a native command queue, not direct Java-thread `epoll_ctl()`.

Required semantics:

1. Java transport code submits `REARM(handle, wantWrite)` or `CLOSE(handle)` only.
2. Submission is thread-safe, non-blocking with respect to epoll ownership, and wakes the native event loop through the existing `eventfd` wake mechanism.
3. The event-loop thread drains commands and performs the corresponding `epoll_ctl()`/native close.
4. Handle lookup and connection lifetime are validated on the native side; stale/closed handles fail safely rather than dereferencing freed memory.
5. A connection event-processing cycle ends with exactly one native decision: rearm or close.
6. Commands submitted after runtime shutdown must fail deterministically and must not touch destroyed runtime state.

This queue is conceptually analogous to the pinned Tomcat `NioEndpoint.Poller`: Tomcat queues `PollerEvent`s, wakes the selector, and lets the poller thread apply registration/interest changes. The NativeTomcat queue must preserve that ownership pattern while replacing the Java `Selector` with the native epoll owner.

## Blocking-read requirement

A direct blocking `recv()` from a Java executor thread is forbidden. Native connections are non-blocking and their readiness is owned by the native event loop. The implementation therefore needs a wait/wakeup mechanism:

1. Java `read(true, ...)` consumes already-buffered bytes first.
2. It attempts native non-blocking read.
3. If native returns `WOULD_BLOCK`, the wrapper records that a blocking read is waiting and requests read interest through the native command boundary.
4. The Java thread waits on wrapper-owned synchronization; it does not wait by calling a blocking native socket operation.
5. The native event loop receives the next readable/peer-close event and wakes the corresponding Java wait path.
6. The wrapper retries the read; EOF and fatal error terminate the wait with the appropriate Java I/O result.
7. The wait state must use a sequence/state transition so an event arriving just before `wait()` cannot be lost.

This follows the pinned Tomcat NIO separation: `NioEndpoint.Poller.processKey()` handles readiness, clears the readiness registration before processing, and wakes `readBlocking` waiters via `readLock`; it does not perform the application read on behalf of the waiting Java thread. Source: pinned Tomcat 11.0.25 `java/org/apache/tomcat/util/net/NioEndpoint.java`, blob `21b0cadbbb3ab04351415c0d7c127ab3500ace58`.

## Tomcat cross-check

The pinned NIO source creates a `NioSocketWrapper`, registers it with the Poller, and keeps selector registration/interest changes inside the Poller thread. `Poller.addEvent()` queues an interest change and calls `selector.wakeup()`. `processKey()` removes the ready operations before dispatch, then either wakes a blocking reader/writer or calls `processSocket()`. NativeTomcat must preserve these ownership and ordering semantics even though the native backend uses epoll. Source: pinned Tomcat 11.0.25 `NioEndpoint.java`, blob `21b0cadbbb3ab04351415c0d7c127ab3500ace58`.

## NGINX cross-check

The official NGINX event abstraction separates readiness, handler dispatch, and event-interest management. Its epoll definitions use `EPOLLIN | EPOLLRDHUP` for read readiness and `EPOLLOUT` for write readiness; its generic event model tracks readiness/state independently. NGINX currently uses `EPOLLET` as its epoll clear-event mode and leaves the `EPOLLONESHOT` definition disabled in the shown backend source. Therefore NativeTomcat's `EPOLLONESHOT` must be justified by NativeTomcat's own ownership/lifecycle design, not presented as an NGINX implementation detail.

Sources: official NGINX `src/event/ngx_event.c` and `src/event/ngx_event.h` on the NGINX repository.

NGINX is an event-architecture reference only; it does not define Servlet semantics.

## Explicit non-goals for this gate

- no `Http11Processor` modification;
- no Servlet/TCK claim;
- no TLS/sendfile/vector-I/O implementation;
- no direct Java-thread `epoll_ctl()`;
- no claim that JNI transport is implemented;
- no benchmark claim.

## Verification requirement

Before this gate can be marked `implemented`, the repository must contain the JNI/native command implementation and focused tests for at least: successful read, `WOULD_BLOCK`, EOF, EINTR, peer half-close, close/lifetime, blocking-read wakeup, exactly-one rearm, stale-handle rejection, shutdown race, and error propagation. After source verification, Ant compilation and runtime tests must be run; prior PASS results from older revisions do not validate this gate.
