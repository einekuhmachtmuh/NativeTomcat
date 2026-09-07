# Native transport gate

This document records the source-verified contract for the next NativeTomcat integration step. It is an implementation gate, not a claim that the transport bridge is complete.

## Current status

- `SocketWrapperBase` is the pinned Tomcat 11.0.25 source at `cbe6e15ee81e2fc6232954292a80cca5d1e84009`; the repository copy is format-normalized and its content has been source-audited.
- `NativeSocketWrapper` is still a surface checkpoint. Its real `read`, `doWrite`, `doClose`, and interest methods are not yet implemented.
- `NativeTransport.java` defines the intended Java/native transport boundary, but no JNI implementation is registered yet.
- Native connection handles are stable `uint64_t` values and are looked up through the native runtime registry.
- Native epoll uses `EPOLLIN | EPOLLRDHUP | EPOLLONESHOT`, with optional `EPOLLOUT`. `EPOLLONESHOT` is a NativeTomcat ownership choice; it is not a claim that NGINX uses the same epoll mode.
- The native runtime has a thread-safe command queue and eventfd wake path for `REARM(handle, wantWrite)` and `CLOSE(handle)`. Submission is rejected after shutdown begins, and commands for one drained batch are coalesced so `CLOSE` dominates and only the final `REARM` state survives otherwise. Connection epoll events carry stable handles rather than native object pointers, and closed handles are no longer returned by the active registry lookup. This is source-implemented but not yet locally compiled/runtime-tested in the current environment.

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

A Java worker must not call `epoll_ctl()` directly. `NativeTransport.rearm()` must therefore enqueue a runtime command rather than become a direct worker-thread `epoll_ctl()` wrapper. Java-side `doClose()` likewise requests native close through the ownership boundary rather than freeing a connection object directly.

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

## Command-queue implementation gate

The native command queue is implemented at the runtime layer:

1. Java-side callers will submit `REARM(handle, wantWrite)` or `CLOSE(handle)` through the native boundary.
2. Submission appends a command under `command_mutex` and wakes the native event loop through the existing `eventfd`.
3. Only the native event-loop thread drains the queue and performs `epoll_ctl()` or native close.
4. Commands use the stable connection handle; native lookup validates that the handle still belongs to the runtime and ignores stale/closed handles without dereferencing freed memory.
5. Within one drained batch, commands for the same handle are coalesced: a `CLOSE` dominates any `REARM`, otherwise only the final `REARM` state is retained. Relative order across distinct handles is preserved.
6. Once `stop_requested` is set, new command submission is rejected with `ECANCELED`, including a second check while holding `command_mutex` to close the stop/enqueue race.
7. Runtime shutdown discards commands before connection destruction; callers must not use the runtime after destruction.
8. Connection epoll events carry the stable handle in `epoll_event.data.u64`; the event loop resolves it through the active registry before dereferencing a connection object. This removes the old dependency on an epoll event retaining a raw `nt_connection_t *` across close processing.

This is deliberately analogous to the pinned Tomcat `NioEndpoint.Poller`: Tomcat queues `PollerEvent`s, wakes the selector, and lets the poller thread apply registration/interest changes. NativeTomcat preserves that ownership pattern while replacing the Java `Selector` with the native epoll owner.

The focused native test covers the command boundary: the first connection event is consumed without a direct rearm from the event callback, the test thread submits `REARM(handle, false)`, a second request is processed, then multiple rearm requests followed by `CLOSE(handle)` are submitted and the connection is verified closed. It also verifies that a closed handle is no longer an active registry result, that commands targeting that closed handle are harmless, and that both request APIs reject with `ECANCELED` after `nt_runtime_stop()`.

The test demonstrates the required final-state semantics, but it does not instrument `epoll_ctl()` to count syscalls. Therefore `exactly one rearm/close` remains a runtime-observability item rather than a measured syscall-count result until an executable test environment is available.

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

The pinned NIO source creates a `NioSocketWrapper`, registers it with the Poller, and keeps selector registration/interest changes inside the Poller thread. `Poller.addEvent()` queues an interest change and calls `selector.wakeup()`. `processKey()` removes the ready operations before processing, then either wakes a blocking reader/writer or calls `processSocket()`. NativeTomcat must preserve these ownership and ordering semantics even though the native backend uses epoll.

## NGINX cross-check

The official NGINX event abstraction separates readiness, handler dispatch, and event-interest management. Its epoll definitions use `EPOLLIN | EPOLLRDHUP` for read readiness and `EPOLLOUT` for write readiness. NGINX currently uses `EPOLLET` as its epoll clear-event mode and leaves the `EPOLLONESHOT` definition disabled in the shown backend source. Therefore NativeTomcat's `EPOLLONESHOT` is justified by NativeTomcat's own ownership/lifecycle design, not presented as an NGINX implementation detail.

NGINX is an event-architecture reference only; it does not define Servlet semantics.

## Explicit non-goals for this gate

- no `Http11Processor` modification;
- no Servlet/TCK claim;
- no TLS/sendfile/vector-I/O implementation;
- no direct Java-thread `epoll_ctl()`;
- no claim that JNI transport is implemented;
- no benchmark claim.

## Verification requirement

Before this gate can be marked `implemented`, focused tests must cover at least: successful read, `WOULD_BLOCK`, EOF, EINTR, peer half-close, close/lifetime, blocking-read wakeup, exactly-one rearm, stale-handle rejection, shutdown race, and error propagation. The command-queue source and focused close/shutdown tests are currently `implemented/source-verified`, but the current revision is **not yet `compiled` or `unit-tested`** because this environment has no usable local Git working tree and ordinary GitHub DNS remains unavailable. GitHub Actions must be checked for a run on the current commit; Ant compilation and runtime tests must still be run when an executable working tree/toolchain is available. Prior PASS results from older revisions do not validate this gate.
