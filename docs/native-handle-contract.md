# Native Handle Contract

## Scope

This document defines the first implementation gate after the pinned `SocketWrapperBase` migration: the lifetime and identity contract between a native connection and the Java transport object.

Reference Tomcat: 11.0.25, pinned commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`.
Reference NGINX event architecture: `src/event/ngx_event.c` and `src/event/ngx_event.h`.

## Verified problem with the previous boundary

The native event callback previously passed `(uint64_t)(uintptr_t)connection` to Java. That value was the address of the native allocation. Although the current runtime retained connection allocations until runtime destruction, the value itself encoded an implementation address rather than an explicit transport identity. It also provided no runtime-owned lookup contract for a Java-side wrapper.

That was insufficient for the required boundary:

```text
stable native-handle <-> Java transport object
```

## Implemented minimum contract

Each `nt_connection_t` now receives a monotonically allocated non-zero `uint64_t` handle at creation time.

- The handle is independent of the connection object's memory address.
- Handles are not intentionally reused during process lifetime.
- `nt_connection_get_handle()` exposes the identity.
- `nt_runtime_find_connection()` resolves a handle through the runtime's connection ownership table.
- The runtime remains the owner of connection allocations until `nt_runtime_destroy()`.
- `main.c` now passes the explicit connection handle to Java dispatch rather than casting the pointer.

The runtime ownership table therefore supplies the lookup owner while the connection object remains alive for the runtime lifetime.

## Registry concurrency gate

The runtime connection table is now protected by a `pthread_mutex_t` for mutation and handle lookup. This is intentionally a small synchronization boundary: it protects the `connections` array and its possible `realloc()` from concurrent Java-worker lookup without introducing a general-purpose global handle map.

`nt_runtime_find_connection()` returns a borrowed `nt_connection_t *`; it does not transfer ownership and does not itself pin the object beyond the lookup. Its safe use therefore depends on the existing lifecycle rule: the runtime must not destroy connection objects until Java-side event processing has drained. `main.c` enforces this ordering by stopping/draining the JVM before `nt_runtime_destroy()`.

This mutex is not an event-loop ownership mechanism. `epoll_ctl()` and rearming remain native event-loop responsibilities.

## I/O correction made at the same gate

`nt_connection_read()` and `nt_connection_write()` now retry `EINTR` rather than classifying it as `WOULD_BLOCK`. `EAGAIN`/`EWOULDBLOCK` remain `WOULD_BLOCK`; EOF remains a distinct result; fatal transport errors retain the existing close behavior.

This is necessary because `WOULD_BLOCK` has event-interest implications and must not be conflated with an interrupted system call.

## Test coverage added

`native/tests/nt_runtime_test.c` verifies that every callback receives a non-zero handle and that the runtime resolves that handle back to the same connection object.

The existing echo/rearm/EOF path remains in the test.

The registry mutex itself is an implementation change and still requires compilation/runtime verification in the current environment before this gate can be marked tested.

## Tomcat cross-check

Pinned Tomcat's NIO wrapper is owned by its endpoint/poller lifecycle and participates in endpoint connection tracking, wrapper state, buffering, interest operations and close/recycle behavior. The native handle is therefore deliberately only an identity/lookup mechanism; it is not presented as a replacement for Tomcat's `SocketWrapperBase` lifecycle contract.

The next gate must connect the resolved native connection to a real `NativeSocketWrapper` instance without bypassing `SocketWrapperBase` state, locking, buffering or processor association.

## NGINX cross-check

NGINX separates event registration/polling from higher-level event handling. This implementation follows the same architectural separation without copying NGINX APIs: the handle identifies the connection, while the native runtime remains the owner of epoll registration and lifetime.

A Java worker must not receive ownership of the epoll registration merely because it receives the handle. The registry mutex likewise protects identity-table memory access only; it does not authorize Java to perform `epoll_ctl()`.

## Explicit non-goals

This gate does not yet implement:

- JNI native read/write methods;
- Java native-method loading;
- `NativeSocketWrapper` transport consumption;
- `isReadyForRead()` semantics;
- write buffering/flush integration;
- read/write-interest requests from Java;
- epoll rearm from Java;
- close requests from Java;
- sendfile;
- TLS;
- vectored I/O;
- `processSocket()` integration.

Those remain subsequent gates and must be implemented only after their ownership and Tomcat contracts are verified.

## Verification status

- stable native identity: **code implemented**;
- runtime-owned handle lookup: **code implemented**;
- registry synchronization: **code implemented; runtime verification pending**;
- EINTR handling correction: **code implemented**;
- source cross-check against pinned Tomcat/NGINX: **source-verified**;
- native unit/runtime test: **not executed in the current environment**;
- Java/Tomcat integration: **not implemented**;
- Servlet/TCK: **not reached**;
- benchmark: **not reached**.
