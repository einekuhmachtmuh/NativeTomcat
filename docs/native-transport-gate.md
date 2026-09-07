# Native transport gate

This document records the source-verified contract for the next NativeTomcat integration step. It is an implementation gate, not a claim that the transport bridge is complete.

## Current status

- `SocketWrapperBase` is the pinned Tomcat 11.0.25 source at `cbe6e15ee81e2fc6232954292a80cca5d1e84009`; the repository copy is format-normalized and its content has been source-audited.
- `NativeSocketWrapper` is still a surface checkpoint. Its real `read`, `doWrite`, `doClose`, and interest methods are not yet implemented.
- `NativeTransport.java` defines the intended Java/native transport boundary, but no JNI implementation is registered yet.
- Native connection handles are stable `uint64_t` values and are looked up through the native runtime registry.
- Native epoll uses `EPOLLIN | EPOLLRDHUP | EPOLLONESHOT`, with optional `EPOLLOUT`. `EPOLLONESHOT` is a NativeTomcat ownership choice; it is not a claim that NGINX uses the same epoll mode.
- The native runtime has a thread-safe command queue and eventfd wake path for `REARM(handle, wantWrite)` and `CLOSE(handle)`. This is source-implemented but not yet locally compiled/runtime-tested in the current environment.

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
5. Queue ordering is FIFO. A rearm and later close therefore cannot be reordered after they have entered the queue.
6. Runtime shutdown currently discards queued commands before connection destruction.

### Re-alignment found during source audit

The current implementation is **not yet sufficient to close the command-queue gate**. Source review found two verification/design gaps that must be fixed before JNI is added:

- `nt_runtime_request_rearm()` / `nt_runtime_request_close()` currently do not reject a request after `nt_runtime_stop()` has begun. The run loop may discard such a command, so submission/shutdown semantics are not yet deterministic.
- The current focused test proves cross-thread rearm through the queue, but does not yet prove `CLOSE` ownership, stale-handle rejection, shutdown race, or exactly-one terminal decision.

Therefore these are the next coding/test gates; do not proceed to JNI merely because the queue compiles.

The lifecycle fix must make command submission reject after shutdown begins, without introducing a runtime-pointer use-after-free. The stale-handle path must be safely ignored/rejected on the native event-loop side without dereferencing freed connections. Tests must exercise both command types and the shutdown boundary.

This remains analogous to the pinned Tomcat `NioEndpoint.Poller`: Tomcat queues `PollerEvent`s, wakes the selector, and lets the poller thread apply registration/interest changes. NativeTomcat preserves that ownership pattern while replacing the Java `Selector` with the native epoll owner.

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

The pinned NIO source creates a `NioSocketWrapper`, registers it with the Poller, and keeps selector registration/interest changes inside the Poller thread. `Poller.addEvent()` queues an interest change and calls `selector.wakeup()`. `processKey()` removes the ready operations before dispatch, then either wakes a blocking reader/writer or calls `processSocket()`. NativeTomcat must preserve these ownership and ordering semantics even though the native backend uses epoll.

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

Before this gate can be marked `implemented`, focused tests must cover at least: successful read, `WOULD_BLOCK`, EOF, EINTR, peer half-close, close/lifetime, blocking-read wakeup, exactly-one rearm, stale-handle rejection, shutdown race, and error propagation. The command-queue source is currently `implemented/source-verified`, but the current revision is **not yet `compiled` or `unit-tested`** because this environment has no usable local Git working tree and ordinary GitHub DNS remains unavailable. GitHub Actions also has no workflow run for the current commit. Ant compilation and runtime tests must therefore be run when an executable working tree/toolchain is available; prior PASS results from older revisions do not validate this gate.
