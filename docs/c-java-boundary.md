# C/Java Boundary Design

## 1. Scope and authority

This document defines the staged C/Java boundary for NativeTomcat. It does not claim that the complete Servlet transport bridge is implemented.

Three authorities remain separate:

- **Servlet 6.1** — application-visible Servlet semantics;
- **Tomcat 11.0.25 pinned source** — Tomcat implementation and integration behavior;
- **NGINX source** — native event-architecture cross-check only.

NativeTomcat design choices in this document are not substitutes for those sources.

## 2. First bridge choice

The first C↔Java transport bridge uses **JNI + direct `ByteBuffer` where safely available**, with a bounded fallback for buffers that cannot be accessed directly.

This is an engineering starting point, not a performance conclusion. JNI and FFM must be compared by measurement if performance becomes a design criterion.

## 3. Ownership model

### Native side owns

- socket descriptor;
- native connection state;
- epoll registration and rearm state;
- event-loop ownership;
- native pending-output storage when required by the transport implementation;
- final native destruction, subject to the Java/native lifetime protocol.

### Java/Tomcat owns

- Tomcat socket-wrapper and processor state;
- HTTP parser/protocol state;
- Catalina/Servlet request and response semantics;
- Java `ByteBuffer` semantic state (`position`, `limit`, `capacity`, mark);
- Servlet readiness and listener sequencing.

Java never directly closes or manipulates the native socket descriptor.

## 4. Buffer boundary

The first transport implementation uses a **bounded borrowed Java `ByteBuffer` window** for socket I/O rather than introducing a persistent native request buffer before differential evidence justifies it.

For one JNI I/O operation:

1. Java determines the permitted buffer window;
2. native code receives only that bounded access;
3. native code performs the non-blocking I/O;
4. native code returns an explicit transfer/status result;
5. Java/Tomcat commits the corresponding buffer/parser state;
6. native code retains no raw buffer pointer after the JNI call.

A temporary native scratch buffer is permitted as a compatibility fallback but must not become persistent Java-visible request state.

## 5. Request/response representation

The initial C boundary exposes transport state, not the complete Tomcat `Request`/`Response` object graph.

| Data | Owner | Boundary representation | Lifetime |
|---|---|---|---|
| socket FD | C | opaque handle only | connection |
| epoll registration | C runtime | no direct Java ownership | event-loop |
| readiness | C runtime | event mask / transport state | event/work item |
| socket I/O window | Java/Tomcat buffer | bounded JNI access | one operation |
| HTTP parser state | Java/Tomcat | normal Tomcat objects/buffers | request/connection |
| Servlet Request/Response | Java | normal Servlet API | Servlet lifecycle |
| Filter/Listener/Async state | Java | normal Servlet API | dispatch/lifecycle |

A native HTTP parser remains deferred.

## 6. Thread affinity and event ownership

Native connection mutation and epoll registration belong to the native event-loop owner unless an explicit marshalling protocol transfers the operation.

A Java worker must not directly call `epoll_ctl` against event-loop-owned state.

The current JNI event path queues Java work through the Tomcat connector Executor. That queueing is not equivalent to transport consumption and therefore cannot by itself justify an `EPOLLONESHOT` rearm.

The eventual bridge must define how a Java transport operation causes the event-loop owner to perform the next rearm.

## 7. JNI reference rules

- `JNIEnv*` is thread-specific and must not be cached globally;
- native threads calling Java must attach to the JVM when necessary and detach before termination;
- persistent Java references must be explicit JNI global references;
- local references must not escape their JNI call scope;
- pending Java exceptions must be checked after throwable JNI operations.

The current JVM bridge already applies these rules to its bootstrap/event-dispatch path; the transport bridge must apply them to connection/wrapper lifetime as well.

## 8. Error and cancellation states

Native connection state:

`ACTIVE -> CLOSING -> CLOSED`

A transport operation additionally needs an explicit result/status model for success, would-block, EOF/half-close and error.

Java/Tomcat must decide how those transport facts affect protocol state, keep-alive, response completion, async processing and Servlet callbacks.

Native cleanup must be idempotent, but final destruction ownership must remain explicit.

## 9. Servlet non-blocking semantics

Native readiness is an implementation input to Tomcat; it is not the Servlet state machine.

In particular:

```text
EPOLLIN  != ServletInputStream.isReady()
EPOLLOUT != ServletOutputStream.isReady()
```

The Java/Tomcat layer must enforce Servlet 6.1 readiness, listener sequencing, buffer state, async processing and completion semantics.

An epoll-readable socket may still be inappropriate for an application read, while parser data already buffered in Java may be available without a fresh epoll event.

## 10. Crossing policy

The initial bridge should prefer coarse-grained operations:

- one event/work item may cover multiple transport bytes;
- read/write data crosses in bounded buffers rather than per-byte JNI calls;
- metadata crosses in bulk where practical;
- native strings and object-field access are not used as a high-frequency transport protocol;
- every throwable JNI call checks exception state.

These are design policies, not measured performance claims.

## 11. Deferred decisions

The following require source-level integration and executable evidence before being frozen:

- exact JNI registration/signature layout;
- native-handle ↔ `SocketWrapperBase` registry and lifetime;
- cross-thread rearm/interest-change queue;
- final native close/destruction handshake;
- HTTP parser ownership;
- CoyoteAdapter integration;
- Servlet request/response implementation details;
- async dispatch integration;
- HTTP/2;
- TLS/OpenSSL;
- FFM alternative;
- persistent buffer pooling;
- sendfile/zero-copy strategy.

## 12. Required verification gates

Before the JNI transport ABI is considered stable:

1. exact affected Tomcat source has been obtained and checked;
2. `SocketWrapperBase` semantics required by the path are implemented or explicitly adapted;
3. native read/write/close/rearm behavior has executable tests;
4. buffer lifetime and aliasing have executable tests;
5. real `processSocket()` / `SocketProcessor` integration works;
6. HTTP incremental parsing works;
7. Servlet 6.1 readiness and `ByteBuffer` semantics pass focused tests;
8. differential tests against pinned Tomcat pass for representative workloads;
9. only then are performance claims measured.

## 13. Explicit non-claims

This document does not claim:

- JNI is faster than FFM;
- direct buffers are always faster than heap buffers;
- direct-buffer use automatically means zero-copy;
- the native HTTP parser is implemented;
- NativeTomcat is already Servlet 6.1 compatible;
- the current JNI event-dispatch path is already Tomcat transport integration;
- final native destruction and Java wrapper lifetime are already race-free.
