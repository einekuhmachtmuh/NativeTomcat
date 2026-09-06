# Tomcat 11.0.25 integration points

## 1. Authority and status

Tomcat implementation behavior in this document is based on the pinned Tomcat 11.0.25 source:

`cbe6e15ee81e2fc6232954292a80cca5d1e84009`

The document describes the verified target execution path and explicitly separates it from the NativeTomcat path that is currently implemented. It is not evidence that the two paths are already equivalent.

## 2. Verified Tomcat execution path

The pinned NIO implementation establishes this processing chain:

```text
NioEndpoint.Poller
    -> processKey()
    -> AbstractEndpoint.processSocket(..., dispatch=true)
    -> endpoint Executor
    -> SocketProcessorBase.run()
    -> endpoint-specific SocketProcessor.doRun()
    -> ProtocolHandler / Http11Processor
    -> Http11Processor.service()
    -> CoyoteAdapter.service()
    -> Catalina / FilterChain / Servlet
```

This separation is a source-verified Tomcat implementation property. The Poller is not the Servlet execution thread.

## 3. Poller and readiness boundary

The pinned `NioEndpoint.Poller.processKey()` handles selector readiness and can dispatch `OPEN_READ` and `OPEN_WRITE` through `processSocket()`.

The important architectural distinction is:

```text
kernel/selector readiness
    != SocketEvent
    != SocketProcessor execution
    != Servlet readiness
```

NativeTomcat must preserve this distinction when replacing the NIO readiness source with epoll.

## 4. `processSocket()` is the integration boundary

The pinned `AbstractEndpoint.processSocket()` is the Tomcat boundary that turns a socket event into processor execution. When dispatching, it uses the endpoint executor and the endpoint-specific socket processor.

Therefore the NativeTomcat event bridge should not directly invoke `Http11Processor`, `Http11Processor.service()`, `CoyoteAdapter`, or Servlet code from the native event callback.

The native event should eventually become the appropriate Tomcat `SocketEvent` for a real `SocketWrapperBase`, after which Tomcat owns the normal processor path.

## 5. Per-connection serialization

The pinned `SocketProcessorBase.run()` acquires the `SocketWrapperBase` lock around processor execution and releases it afterwards.

NativeTomcat's current `NativeEventDispatcher` independently serializes queued Java work per opaque native handle. That is a useful concurrency property, but it is not a substitute for the real Tomcat wrapper lock because the current event task does not yet invoke `SocketProcessorBase`.

The final integration must ensure that there is one coherent serialization rule rather than two independently assumed locks that can deadlock or permit races.

## 6. Socket wrapper lifecycle

The pinned Tomcat implementation associates the socket wrapper with endpoint connection management and performs close/recycle work through the endpoint and concrete wrapper implementation.

This means native connection lifetime cannot be reduced to:

```text
JNI callback returns -> destroy native connection
```

The lifetime must cover pending event registration, Java processing, wrapper state, pending output, and terminal cleanup.

The current native runtime's conservative retention of connection objects until runtime shutdown is therefore a safety checkpoint, not a final Tomcat lifecycle implementation.

## 7. Read path

Tomcat's transport abstraction exposes both byte-array and `ByteBuffer` reads. The NIO implementation can consume data from its socket buffer and, where appropriate, read into a destination `ByteBuffer`.

`Http11InputBuffer` maintains incremental parser state. One socket read is therefore not equivalent to one HTTP request, header block, or application read.

NativeTomcat's first transport adapter should preserve Tomcat's existing parser/buffer semantics rather than introduce a native HTTP parser as an unverified shortcut.

## 8. Write path

Tomcat's socket abstraction supports non-blocking writes, buffering and write-interest registration. A write operation can consume only part of the supplied data.

Therefore the native bridge must represent at least:

- bytes accepted by the native write operation;
- bytes remaining in Tomcat/native pending output;
- whether write readiness is currently required;
- final close/error state.

A Java write returning successfully must not be documented as proof that the peer has received all bytes.

## 9. HTTP processing boundary

The pinned `Http11Processor.service()` performs protocol processing and eventually invokes the configured adapter. Request preparation includes protocol and security-sensitive validation before adapter dispatch.

This is why the initial NativeTomcat architecture keeps HTTP semantics in Tomcat Java. Replacing the HTTP parser or processor with native code requires differential behavioral evidence, not merely equivalent-looking output for one smoke request.

## 10. Servlet boundary

The application-visible Servlet contract belongs above Coyote/Catalina transport internals. Native readiness therefore cannot directly define `ServletInputStream.isReady()` or `ServletOutputStream.isReady()`.

Servlet 6.1 readiness and listener sequencing must continue to be enforced by the Java/Tomcat layer.

## 11. Current NativeTomcat path

The repository currently implements only the prefix:

```text
native epoll
    -> native event callback
    -> JNI dispatchNativeEvent(handle, events)
    -> NativeEventDispatcher
    -> Tomcat connector Executor
    -> queued Java task
```

The current Java task does **not** yet continue into:

```text
SocketWrapperBase
    -> AbstractEndpoint.processSocket()
    -> SocketProcessorBase
    -> ProtocolHandler
    -> Http11Processor
```

This gap is intentional and must remain explicit in project status.

## 12. Required insertion point

The next real transport integration must establish, in order:

1. exact pinned `SocketWrapperBase` implementation/dependencies;
2. native-handle-to-wrapper ownership and lifetime;
3. native read/write/close operations with bounded buffer semantics;
4. event-to-`SocketEvent` mapping;
5. real `AbstractEndpoint.processSocket()` behavior;
6. real `SocketProcessorBase` execution;
7. only then `ProtocolHandler` / `Http11Processor` execution.

Any missing upstream class is a source-verification gate under `docs/tomcat-source-migration-rule.md`.

## 13. NGINX cross-check

NGINX is useful for checking the native event architecture: readiness polling, registration/rearming and handler separation are distinct concerns. It does not define Tomcat's `SocketWrapperBase`, processor, executor, HTTP, or Servlet semantics.

The resulting rule is:

```text
NGINX event architecture -> native design cross-check
Tomcat 11.0.25 source    -> Tomcat implementation authority
Servlet 6.1 specification -> application-visible semantics
```

## 14. Explicit non-claims

The current repository does not yet prove:

- a native epoll event reaches `SocketWrapperBase`;
- `AbstractEndpoint.processSocket()` is the actual NativeTomcat execution path;
- `Http11Processor` is invoked from native transport;
- Servlet requests are served through native transport;
- native and Tomcat close/recycle lifetimes are equivalent;
- native buffering is semantically equivalent to Tomcat buffering;
- JNI is faster than FFM.
