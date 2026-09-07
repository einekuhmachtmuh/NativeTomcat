# Native transport gate

This document records the source-verified contract for the current NativeTomcat integration step. It is an implementation gate, not a claim that the transport path is runtime-verified.

## Current status

- `SocketWrapperBase` is the pinned Tomcat 11.0.25 source at `cbe6e15ee81e2fc6232954292a80cca5d1e84009`; the repository copy is format-normalized and its content has been source-audited.
- `NativeSocketWrapper` now consumes the JNI transport bridge for non-blocking read/write, owns Tomcat socket buffers, implements blocking read/write wait state, maps EOF to `EOFException`, propagates transport errors, wakes blocked I/O on native readiness/close/error, and requests interest/close through the native ownership boundary. This is `implemented/source-verified`, not yet `compiled` or runtime-tested.
- `NativeTransport.java` defines the Java/native transport boundary. The JNI implementation in `native/src/nt_native_transport.c` resolves active connections by stable handle and routes rearm/close through the native runtime command queue. It is source-implemented but not yet compiled or runtime-tested in the current environment.
- The JNI transport runtime binding is protected by a read/write lock. The runtime remains bound through native event-loop execution and JVM shutdown, then is unbound before `nt_runtime_destroy()`. This prevents runtime destruction from racing an in-flight Java transport call.
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

`SocketWrapperBase` itself owns the higher-level read/write buffer protocol. In particular, blocking and non-blocking writes use the socket write buffer and the non-blocking write buffer before calling `doWrite()`. `NativeSocketWrapper` therefore consumes those Tomcat buffers rather than bypassing them merely to reach `send(2)`.

The pinned NIO implementation also distinguishes Java/Tomcat read semantics from kernel readiness. A non-blocking read can return no data; a blocking read waits for a future readiness event. EOF is distinct from `WOULD_BLOCK` and must reach Tomcat close/read semantics rather than being treated as ordinary zero-byte data.

## Latest NioSocketWrapper source audit

The pinned Tomcat 11.0.25 `NioEndpoint.NioSocketWrapper` was re-audited before implementing this gate:

1. `read(boolean, byte[])` first consumes Tomcat's socket read buffer, then calls `fillReadBuffer(block)` and updates the last-read timestamp. `read(boolean, ByteBuffer)` fills the Tomcat read buffer when it cannot use the direct channel path.
2. `fillReadBuffer(block, buffer)` performs the actual non-blocking channel read. `-1` becomes `EOFException`; `0` in blocking mode sets `readBlocking`, calls `registerReadInterest()`, and waits on `readLock`. It does not perform a blocking kernel read on the Java worker thread.
3. `doWrite(block, buffer)` uses the same separation in the write direction: blocking mode waits on `writeLock` after registering write interest when the non-blocking channel cannot progress; non-blocking mode stops when the channel cannot accept more data and relies on write readiness to continue.
4. `registerReadInterest()` and `registerWriteInterest()` enqueue interest changes into the Tomcat Poller. The Poller owns selector registration and wakeup. This is the direct Tomcat precedent for NativeTomcat's native command queue.
5. `doClose()` removes the connection from the endpoint registry, closes the channel, releases/reset buffers and cached channel state, and closes pending sendfile state. Native close therefore has to preserve both transport lifetime and Tomcat wrapper cleanup semantics.
6. `isReadyForRead()` first exposes already-buffered application data and otherwise attempts a non-blocking fill; it is not equivalent to the native kernel `EPOLLIN` bit.

The audit supports the implemented Java step: **transport consumption plus wait/wakeup state**. `Http11Processor` remains deferred until the real `processSocket → SocketProcessorBase → transport consumption` path is source-verified and executable.

## Native ownership contract

The native event loop is the sole owner of:

- epoll registration;
- readiness interest;
- `EPOLLONESHOT` rearm;
- native connection close/destroy;
- native connection registry lifetime.

A Java worker must not call `epoll_ctl()` directly. `NativeTransport.rearm()` enqueues a runtime command rather than becoming a direct worker-thread `epoll_ctl()` wrapper. `NativeSocketWrapper.doClose()` likewise requests native close through the ownership boundary rather than freeing a connection object directly.

## Blocking wait/wakeup implementation

`NativeSocketWrapper` now follows the pinned Tomcat NIO ownership model at the transport boundary:

1. Java `read(true, ...)` consumes already-buffered bytes first.
2. It attempts a native non-blocking read through `NativeTransport.read()`.
3. If native returns `WOULD_BLOCK`, the wrapper changes `readBlocking` from false to true while holding `readLock`, then requests read interest through `NativeTransport.rearm(handle, false)` and waits on the wrapper lock.
4. Java does not call a blocking native socket operation.
5. `NativeEndpoint.processNativeEvent()` observes the blocking state before dispatch, lets the wrapper wake the corresponding waiter, and suppresses a duplicate `OPEN_READ`/`OPEN_WRITE` processor for that blocked direction.
6. The wrapper retries the transport operation after wakeup; EOF and fatal native errors terminate the wait with the appropriate Java I/O result.
7. Close wakes both wait paths before requesting native close, preventing a Java worker from remaining asleep after wrapper shutdown.
8. The read/write blocking state is changed and checked under the same lock used by `wait()`/`notify()`, so readiness arriving immediately before `wait()` cannot be lost through a check-then-sleep race.

This is the key semantic distinction from a simple callback bridge: kernel readiness wakes a Java transport consumer when one is already blocked; it is not automatically converted into a second concurrent Tomcat processor.

## Required event cycle

The target cycle is:

```text
kernel readiness
    -> native epoll event
    -> stable handle
    -> Java dispatcher / SocketProcessor OR blocked-I/O wakeup
    -> SocketWrapperBase transport consumption
    -> Java reports resulting interest/close state
    -> native event-loop command queue
    -> exactly one rearm OR close
```

`Java task submitted` is not `transport consumed`. The native event loop must not rearm immediately after scheduling Java work, because the Java processor may still be consuming data or may close the wrapper.

## Command-queue implementation gate

The native command queue is implemented at the runtime layer:

1. Java-side callers submit `REARM(handle, wantWrite)` or `CLOSE(handle)` through the native boundary.
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

## Tomcat cross-check

The pinned NIO source creates a `NioSocketWrapper`, registers it with the Poller, and keeps selector registration/interest changes inside the Poller thread. `Poller.addEvent()` queues an interest change and calls `selector.wakeup()`. `processKey()` removes the ready operations before processing, then either wakes a blocking reader/writer or calls `processSocket()`. NativeTomcat now preserves that ownership distinction: blocked transport consumption is woken instead of receiving a duplicate processor dispatch.

## NGINX cross-check

The official NGINX event abstraction separates readiness, handler dispatch, and event-interest management. Its epoll definitions use `EPOLLIN | EPOLLRDHUP` for read readiness and `EPOLLOUT` for write readiness. NGINX currently uses `EPOLLET` as its epoll clear-event mode and leaves the `EPOLLONESHOT` definition disabled in the shown backend source. Therefore NativeTomcat's `EPOLLONESHOT` is justified by NativeTomcat's own ownership/lifecycle design, not presented as an NGINX implementation detail.

NGINX's event layer also keeps readiness management separate from connection/request consumption. This supports the separation used here: readiness notification is not itself transport consumption or Servlet readiness.

NGINX is an event-architecture reference only; it does not define Servlet semantics.

## Explicit non-goals for this gate

- no `Http11Processor` modification;
- no Servlet/TCK claim;
- no TLS/sendfile/vector-I/O implementation;
- no direct Java-thread `epoll_ctl()`;
- no benchmark claim.

## Verification requirement

The source-implementation gate now covers: successful transport read/write paths, `WOULD_BLOCK` handling, EOF/error mapping, blocking wait/wakeup state, peer-half-close wakeup, close wakeup, and native ownership routing. It does **not** yet have executable proof for actual socket I/O, timeout behavior, race stress, exactly-one `epoll_ctl()` per event cycle, or integration with `Http11Processor`.

The current revision is therefore `implemented/source-verified`, not `compiled`, `unit-tested`, `integration-tested`, `Servlet/TCK-tested`, or `benchmark-verified`. The environment still has no usable local Git working tree and ordinary GitHub DNS remains unavailable. Ant compilation and runtime tests must be run when an executable working tree/toolchain is available. Prior PASS results from older revisions do not validate this gate.
