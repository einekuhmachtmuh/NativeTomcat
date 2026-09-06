# Tomcat 11.0.25 SocketWrapperBase Contract

## 1. Scope and verification status

This document records the transport contract that NativeTomcat must preserve before introducing a native-backed Tomcat `SocketWrapperBase` implementation.

The contract is derived from the pinned Tomcat 11.0.25 source. It is an implementation contract, not a claim that every item is required by the Jakarta Servlet specification.

Verified source points:

- `SocketWrapperBase.java`
- `SocketProcessorBase.java`
- `NioEndpoint.java`
- `AbstractEndpoint.java`
- `Http11Processor.java`

The first native implementation will intentionally support only the subset needed for the initial HTTP/1.1 transport proof. Features marked `DEFERRED` must not be silently represented as if they were implemented.

## 2. Connection ownership model

```text
Native connection state
    owns FD / epoll registration / native buffers
            |
            | JNI-controlled association
            v
Tomcat SocketWrapperBase-compatible Java object
    owns Java-visible transport state
            |
            v
Http11Processor
    owns HTTP/1.1 protocol state
            |
            v
CoyoteAdapter -> Catalina -> Servlet
```

The native connection object must outlive any Java operation that can still reference its direct buffers or schedule a read/write interest. A Java callback returning is therefore not a sufficient destruction point.

## 3. Verified SocketWrapperBase surface

| Surface | Tomcat 11.0.25 contract | Initial NativeTomcat treatment |
|---|---|---|
| Wrapped socket | `getSocket()` exposes the endpoint-specific socket object | Do not expose the native FD directly to application code; Java adapter owns an opaque native handle |
| Per-connection lock | `getLock()` returns a `ReentrantLock`; `SocketProcessorBase.run()` holds it across `doRun()` | Preserve one serialized Java protocol-processing domain per connection |
| Closed state | Atomic `closed`; `close()` is idempotent and invokes handler release before concrete close | Native close must be idempotent and coordinated with Java wrapper state |
| Read timeout | `setReadTimeout()` / `getReadTimeout()` | Mirror value into native timer state only if native transport owns the timeout |
| Write timeout | `setWriteTimeout()` / `getWriteTimeout()` | Same rule as read timeout |
| Keep-alive count | `setKeepAliveLeft()` / `decrementKeepAlive()` | Keep protocol accounting in Java initially |
| Peer/local addresses | remote/local host, address and port getters | Java-visible metadata may be populated from native connection creation; ownership remains Java for cached strings |
| Socket buffers | `getSocketBufferHandler()` plus internal read/write buffers | Do not introduce a second buffer model; use direct `ByteBuffer` where native memory is intentionally shared |
| Read readiness | `isReadyForRead()` | Transport readiness must be translated into the Java protocol/readiness state; never equate epoll readiness with Servlet `isReady()` |
| Write readiness | `isReadyForWrite()` / `canWrite()` | Native writable event only drives transport progress and Java notification/dispatch |
| Read | `read(boolean, byte[], int, int)` and `read(boolean, ByteBuffer)` | Initial bridge should prefer `ByteBuffer` for bulk transfer and avoid per-byte JNI calls |
| App read buffer handler | `setAppReadBufHandler(ApplicationBufferHandler)` | Required for compatibility with Tomcat's application-buffer path; do not fake it with an unrelated native buffer API |
| Push-back | `unRead(ByteBuffer)` | Required for protocol upgrade/over-read correctness; initially Java-owned buffering is safer |
| Write | `write(boolean, byte[], int, int)` and `write(boolean, ByteBuffer)` | Preserve blocking/non-blocking distinction and ByteBuffer ownership rules |
| Flush | `flush(boolean)` | Must report whether non-blocking output remains pending |
| Write-interest | `registerWriteInterest()` | Must arm native writable interest without losing pending bytes |
| Read-interest | `registerReadInterest()` | Must arm native readable interest without duplicate/lost wakeups |
| Socket events | `processSocket(SocketEvent, boolean)` | Native events enter Tomcat's dispatch path rather than directly invoking Servlet code |
| Sendfile | `createSendfileData()` / `processSendfile()` | `DEFERRED` for first milestone; must remain explicit rather than returning fake success |
| TLS support | `doClientAuth()` / `getSslSupport()` | `DEFERRED`; native plain HTTP/1.1 proof must not advertise TLS support |
| Async vectored I/O | `BlockingMode`, `CompletionState`, `CompletionCheck`, operation state and related methods | `DEFERRED`; do not implement a partial fake API |
| Error state | first I/O error can be recorded and later checked | Native errors must have a deterministic translation into Java `IOException` / close state |
| Current processor | atomic processor association | Preserve only if required by the selected Tomcat processor path |
| Negotiated protocol/SNI | getters/setters | `DEFERRED` with TLS/HTTP2; must not be invented for plain HTTP/1.1 |

## 4. Read semantics

`SocketWrapperBase.read(...)` is not equivalent to a raw `recv()` call. Tomcat may first consume bytes already present in its internal read buffer and may transfer between internal and application buffers. The ByteBuffer overload is particularly important because the NIO implementation can use the destination directly in suitable cases.

NativeTomcat therefore uses the following initial rule:

1. native transport fills a bounded native/read buffer;
2. Java receives a direct `ByteBuffer` view when sharing is safe;
3. Java protocol code remains responsible for HTTP parsing and semantic consumption;
4. buffer lifetime is pinned until Java has finished the operation;
5. no native buffer is recycled while a Java ByteBuffer can still reference it.

## 5. Write semantics

Tomcat's non-blocking write path can retain data in both the socket write buffer and a separate `WriteBuffer`. A non-blocking call may therefore accept data into buffering without having transmitted all bytes to the network.

NativeTomcat must model at least:

```text
APPLICATION_DATA
    -> JAVA/TOMCAT BUFFER
    -> NATIVE PENDING BUFFER
    -> KERNEL SEND BUFFER
    -> peer/network
```

The completion of a Java write operation must not be reported as peer receipt. For non-blocking writes, the native connection remains live until all required buffered bytes are either transmitted, explicitly abandoned by a defined close/error transition, or handed back to the appropriate Java state machine.

## 6. Readiness and serialization

`NioEndpoint.Poller.processKey()` can generate `OPEN_READ` and `OPEN_WRITE` processing for the same connection. `AbstractEndpoint.processSocket()` creates or reuses a `SocketProcessor` and dispatches it to the endpoint executor when requested. `SocketProcessorBase.run()` then serializes processing with the wrapper's `ReentrantLock`.

NativeTomcat therefore requires:

- native readiness may be detected independently for read and write;
- Java protocol processing for one logical connection remains serialized;
- native event delivery must not bypass Tomcat's event/processor state machine;
- concurrent native callbacks must not create concurrent mutation of one HTTP/1.1 processor state.

## 7. Close and cancellation state machine

The minimum native state is:

```text
ACTIVE
  |
  +--> CLOSING
  |      |
  |      +--> CLOSED
  |
  +--> FAILED --> CLOSING --> CLOSED
```

Additional transient conditions such as `READ_PENDING`, `WRITE_PENDING`, or `JAVA_DISPATCHED` are attributes/guards rather than independent terminal states.

Required invariants:

1. `CLOSED` is terminal.
2. FD close occurs at most once.
3. epoll interest is removed or made harmless before native memory is reclaimed.
4. Java direct-buffer views cannot outlive the native allocation they reference.
5. a queued Java processor must observe closure before using transport state.
6. shutdown must wake the event loop and converge all connections to a terminal state.

## 8. Servlet readiness is a separate layer

The transport layer's readable/writable state is not the Servlet API's `ServletInputStream.isReady()` / `ServletOutputStream.isReady()` state.

The native layer may therefore provide an event such as:

```text
EPOLLIN
    -> transport has bytes
    -> schedule/continue Tomcat processing
    -> Tomcat input state determines Servlet readiness
```

and similarly for output. NativeTomcat must not expose `EPOLLIN` or `EPOLLOUT` as Servlet readiness directly.

## 9. JNI representation

For the first bridge, the preferred representation is:

| Value | Representation | Owner | Lifetime |
|---|---|---|---|
| Native connection | opaque `jlong` handle | C | until CLOSED and all Java references released |
| Native byte storage | native allocation | C | until no Java direct view can reference it |
| Java byte view | direct `ByteBuffer` | Java reference; storage owned by C | bounded by native lease |
| Request/response semantics | existing Tomcat objects | Java/Tomcat | normal Tomcat lifecycle |
| FD | native-only integer | C | until close |
| Read/write event | native event + Java `SocketEvent` dispatch | C creates event, Java consumes dispatch | one dispatch lifecycle |

The first implementation must not store a raw `JNIEnv *` globally. Any native thread that calls Java must use the correct attached JNI environment, and persistent Java references must use the appropriate global-reference lifetime rules.

## 10. Copy/no-copy policy

No-copy is an optimization, not a correctness requirement.

The implementation may copy when required for:

- buffer ownership transfer;
- lifetime isolation;
- compaction/reassembly;
- protocol upgrade push-back;
- TLS processing;
- shutdown/error cleanup.

A direct buffer is justified only when its lifetime and mutability are explicit and the measured copy avoidance is beneficial.

## 11. Initial implementation subset

The first concrete transport adapter is approved to implement only:

- plain TCP;
- IPv4/IPv6 socket metadata needed by Tomcat;
- non-blocking read/write;
- direct ByteBuffer bulk transfer;
- read/write interest registration;
- connection close/error;
- per-connection serialization;
- keep-alive through the existing Java protocol state;
- clean shutdown.

The following are explicitly out of scope for that first adapter:

- TLS/OpenSSL;
- HTTP/2;
- WebSocket upgrade;
- sendfile;
- NIO2-style vectored async APIs;
- native HTTP parsing;
- native Servlet dispatch;
- native Session/JSP/Catalina implementation.

## 12. Acceptance criteria before coding the adapter

The adapter must not be considered complete until tests demonstrate:

1. connection creation and close are idempotent;
2. repeated readable events do not concurrently enter one Java processor;
3. partial non-blocking writes preserve byte order and all remaining bytes;
4. direct-buffer memory remains valid for the complete Java access interval;
5. EOF, reset, timeout and native I/O errors converge to defined Java-visible states;
6. keep-alive permits multiple requests without connection-state corruption;
7. shutdown wakes the native event loop and does not leave registered FDs or native allocations behind;
8. malformed HTTP remains handled by Tomcat's Java parser, not by an unverified native parser;
9. all Java/C ownership transitions are observable in tests;
10. no benchmark claim is made until the same workload can be run against the unmodified Tomcat baseline.

## 13. Verification conclusion

The source inspection confirms that `SocketWrapperBase` is a substantial transport/state abstraction rather than a thin file-descriptor wrapper. The native integration point must therefore be designed around its observable behavior and lifecycle rather than around a C struct that merely contains an FD.

The next implementation step is a **minimal Java transport adapter skeleton plus native handle lifecycle**, not a native HTTP parser and not a Servlet implementation. That skeleton should compile through the Tomcat Ant build when the required JDK/build environment is available; no local full-build success is claimed at this stage.
