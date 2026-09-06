# SocketWrapperBase surface and migration status

## 1. Purpose

This document records the current repository-owned `SocketWrapperBase` compatibility surface and the gate for replacing it with the exact Tomcat 11.0.25 implementation.

Reference:

`Tomcat 11.0.25 / cbe6e15ee81e2fc6232954292a80cca5d1e84009`

This document must never be read as proof that the compatibility shell has Tomcat transport semantics.

## 2. Current repository surface

The current type relationship is:

```text
SocketWrapperBase<E>
    |
    +-- NativeSocketWrapper
    |
    +-- SocketProcessorBase<S>
            |
            +-- NativeSocketProcessor
```

The surface contains representative categories for identity, endpoint association, locking, processor association, close state, read/write APIs, readiness, buffer handling, timeout, keep-alive, metadata, errors, interest registration and deferred advanced I/O.

The surface test proves only the properties it actually exercises, especially per-wrapper serialization. It does not prove behavioral equivalence with Tomcat 11.0.25.

## 3. Current implementation status

### Present as compatibility surface

- opaque native socket identity;
- endpoint association;
- per-connection lock;
- current processor association;
- close/error state surface;
- byte-array and `ByteBuffer` read/write method shapes;
- read/write readiness method shapes;
- read/write interest method shapes;
- flush and timeout method shapes;
- application read-buffer handler surface;
- protocol/SNI/address metadata surface.

### Explicitly incomplete

`NativeSocketWrapper` still contains deferred or placeholder behavior for the operations that actually touch the native transport, including read, write and final native close/lifetime handling. Interest registration is not yet a complete cross-thread event-loop protocol.

Advanced Tomcat behavior remains deferred where applicable:

- sendfile;
- TLS/SSL support;
- vectored asynchronous I/O;
- push-back/unread;
- protocol upgrade.

## 4. Why the shell is insufficient

The pinned `SocketWrapperBase` contains materially more than a descriptor wrapper. Its behavior participates in:

- endpoint/executor dispatch;
- connection locking;
- application and socket buffers;
- non-blocking read/write state;
- async operation state;
- processor association;
- timeout and keep-alive state;
- error propagation;
- close/recycle lifecycle;
- Servlet connection metadata;
- advanced transport paths.

Consequently matching method names is not enough. The exact method bodies and supporting classes must be migrated or explicitly adapted after source-level comparison.

## 5. Exact-source gate

The pinned upstream `SocketWrapperBase.java` has been source-inspected and its blob identity recorded during the project audit. The repository copy is **still a compatibility shell**, not the exact upstream implementation.

Therefore the migration gate remains open.

Before replacing the shell, the project must:

1. obtain/materialize the exact pinned source in the repository;
2. verify its package/path/content;
3. resolve every required supporting Tomcat class recursively;
4. update both `build.xml` and `build_test.xml`;
5. compile the migrated source;
6. preserve or update focused tests to cover the new semantics;
7. compare the resulting behavior with the pinned source before advancing to `NativeSocketWrapper` transport integration.

If the exact source cannot be materialized, do not hand-recreate it.

## 6. NIO reference path

The pinned Tomcat NIO architecture is:

```text
NioEndpoint
    -> NioSocketWrapper extends SocketWrapperBase<NioChannel>
    -> Poller registration/readiness
    -> AbstractEndpoint.processSocket()
    -> SocketProcessorBase
```

NativeTomcat is replacing the readiness/transport implementation, not the Tomcat protocol semantics. The adapter therefore has to reproduce the `SocketWrapperBase` contract required by the normal Tomcat processing path.

## 7. Event and readiness boundary

A native epoll event is not itself a `SocketEvent`, a processor invocation, or a Servlet readiness notification.

The final mapping must preserve:

```text
kernel readiness
    -> native event handling
    -> Tomcat SocketEvent / processSocket
    -> SocketProcessor
    -> protocol processing
    -> Servlet readiness semantics
```

This is also consistent with the NGINX cross-check, where event polling and handler dispatch remain distinct concerns.

## 8. Serialization requirement

The current `SocketProcessorBase` surface preserves the important Tomcat property that processing for one wrapper is serialized by the wrapper lock.

The existing surface test demonstrates `maxActive == 1` for concurrent processor submissions on one wrapper. This is a **unit/surface verification**, not an integration proof.

When the real upstream `SocketProcessorBase` and `SocketWrapperBase` are introduced, the test must be rerun against the real implementation rather than assumed to remain valid.

## 9. Integration order

The migration and integration sequence is:

```text
exact SocketWrapperBase
    -> required supporting classes
    -> NativeSocketWrapper real I/O
    -> native handle/wrapper lifetime
    -> event -> SocketEvent mapping
    -> real AbstractEndpoint.processSocket()
    -> real SocketProcessorBase
    -> ProtocolHandler
    -> Http11Processor
    -> CoyoteAdapter / Catalina / Servlet
```

No later stage is considered complete merely because an earlier type compiles.

## 10. Explicit non-claims

This document does not claim:

- exact upstream `SocketWrapperBase` migration is complete;
- `NativeSocketWrapper` implements Tomcat NIO behavior;
- native epoll readiness is Servlet readiness;
- native I/O is zero-copy;
- the current lifetime protocol is race-free under full Java integration;
- HTTP/1.1 processing already reaches the native wrapper.
