# Native transport gate

This document records the source-verified contract for the next NativeTomcat integration step. It is an implementation gate, not a claim that the transport bridge is complete.

## Current status

- `SocketWrapperBase` is the pinned Tomcat 11.0.25 source at `cbe6e15ee81e2fc6232954292a80cca5d1e84009`; the repository copy is format-normalized and its content has been source-audited.
- `NativeSocketWrapper` is still a surface checkpoint. Its real `read`, `doWrite`, `doClose`, and interest methods are not yet implemented.
- `NativeTransport.java` now defines the intended Java/native transport boundary, but no JNI implementation is registered yet.
- Native connection handles are stable `uint64_t` values and are looked up through the native runtime registry.
- Native epoll uses `EPOLLIN | EPOLLRDHUP | EPOLLONESHOT`, with optional `EPOLLOUT`.

## Tomcat contract

The pinned `SocketWrapperBase` contract requires transport implementations to provide:

- `read(boolean, byte[])` and `read(boolean, ByteBuffer)`;
- `isReadyForRead()`;
- `doWrite(boolean, ByteBuffer)`;
- `flushNonBlocking()`;
- `registerReadInterest()` and `registerWriteInterest()`;
- `doClose()`.

`SocketWrapperBase` itself owns the higher-level read/write buffer protocol. In particular, blocking and non-blocking writes use the socket write buffer and the non-blocking write buffer before calling `doWrite()`. A NativeSocketWrapper must therefore not bypass those buffers merely to reach `send(2)`.

The pinned NIO implementation also distinguishes Java/Tomcat read semantics from kernel readiness. A non-blocking read can return no data; a blocking read waits for a future readiness event. EOF is distinct from `WOULD_BLOCK` and must reach the Tomcat close/read semantics rather than being treated as ordinary zero-byte data.

## Native ownership contract

The native event loop is the sole owner of:

- epoll registration;
- readiness interest;
- `EPOLLONESHOT` rearm;
- native connection close/destroy;
- native connection registry lifetime.

A Java worker must not call `epoll_ctl()` directly. Therefore `NativeTransport.rearm()` must not be implemented as a direct worker-thread `epoll_ctl()` wrapper. If Java processing changes interest, it must submit a command to the native event-loop owner, which performs the actual rearm.

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

## Blocking-read requirement

A direct blocking `recv()` from a Java executor thread is forbidden. Native connections are non-blocking and their readiness is owned by the native event loop. The implementation therefore needs a wait/wakeup mechanism:

1. Java `read(true, ...)` consumes already-buffered bytes first.
2. It attempts native non-blocking read.
3. If native returns `WOULD_BLOCK`, the wrapper requests read interest through the native command boundary and waits for a subsequent native readable/peer-close event.
4. The native event loop remains the only component that performs epoll rearm.
5. The next native event wakes the corresponding Java wait path; the wrapper retries the read.
6. EOF and fatal error terminate the wait with the appropriate Java I/O result.

This is the minimum needed to preserve the pinned Tomcat distinction between blocking request-body reads and non-blocking/read-listener operation.

## NGINX cross-check

The official NGINX event abstraction separates event readiness, handler dispatch, and event-interest management. On epoll builds its read event includes `EPOLLIN | EPOLLRDHUP`, while write readiness is `EPOLLOUT`; the event structure tracks `active`, `ready`, `oneshot`, `eof`, and `error` independently. NativeTomcat therefore follows the same architectural separation but keeps Tomcat's Java transport and Servlet semantics above the native event layer.

NGINX is an event-architecture reference only; it does not define Servlet semantics.

## Explicit non-goals for this gate

- no `Http11Processor` modification;
- no Servlet/TCK claim;
- no TLS/sendfile/vector-I/O implementation;
- no direct Java-thread `epoll_ctl()`;
- no claim that JNI transport is implemented;
- no benchmark claim.

## Verification requirement

Before this gate can be marked `implemented`, the repository must contain the JNI/native command implementation and focused tests for at least: successful read, `WOULD_BLOCK`, EOF, EINTR, peer half-close, close/lifetime, blocking-read wakeup, exactly-one rearm, and error propagation. After source verification, Ant compilation and runtime tests must be run; prior PASS results from older revisions do not validate this gate.
