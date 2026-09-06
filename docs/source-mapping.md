# Upstream source mapping

This document records source-level facts verified against Apache Tomcat 11.0.25 and NGINX 1.30.4. It is limited to inspected paths and does not claim a complete implementation inventory.

## Tomcat request path

| Stage | Upstream implementation | Verified fact | NativeTomcat decision |
|---|---|---|---|
| Connector | `java/org/apache/catalina/connector/Connector.java` | The default `Connector()` selects HTTP/1.1 and protocol creation is delegated to `ProtocolHandler.create()` | Keep Catalina/Connector lifecycle in Java |
| Protocol | `java/org/apache/coyote/AbstractProtocol.java`, `AbstractHttp11Protocol.java` | `AbstractProtocol` owns an `AbstractEndpoint`; an `Adapter` links protocol processing to the container | Preserve the Java protocol/adapter boundary initially |
| Endpoint | `java/org/apache/tomcat/util/net/AbstractEndpoint.java`, `NioEndpoint.java` | Endpoint lifecycle includes acceptor, connection tracking, processor/event state and executor management | Native transport may replace low-level I/O only after lifecycle equivalence is demonstrated |
| Accept | `NioEndpoint.serverSocketAccept()` / `Acceptor.run()` | The acceptor accepts sockets, checks endpoint state and passes them to socket-option setup | C accept/event loop is a candidate |
| Socket registration | `NioEndpoint.setSocketOptions()` | A channel/wrapper is configured and registered with the Poller | Native connection state must have explicit ownership/lifetime |
| Poller | `NioEndpoint.Poller` | Selector events are registered, wakeups are issued and readable/writable events are dispatched | Strong native candidate; Java semantic events remain above the boundary |
| Socket wrapper | `SocketWrapperBase` / `NioSocketWrapper` | The wrapper carries timeout, buffer, keep-alive, error and non-blocking I/O state | Do not translate fields one-for-one; define a smaller native transport state |
| HTTP parsing | `Http11InputBuffer` | Request-line/header parsing is incremental and stateful; the parser consumes a Java `ByteBuffer` through the socket wrapper | Keep HTTP parser and parser-owned buffers in Java initially |
| Request preparation | `Http11Processor.prepareRequest()` | It validates Host, URI, transfer encoding, content length, SNI and related protocol state | Keep security-sensitive protocol semantics in Java initially |
| Servlet dispatch | `CoyoteAdapter.service()` | It creates/links Catalina Request/Response objects and invokes the container pipeline | Keep in Java |
| Servlet/filter execution | `StandardWrapperValve.invoke()` | It allocates the servlet, creates the application filter chain and invokes `filterChain.doFilter()` | Keep in Java |
| Async dispatch | `CoyoteAdapter.asyncDispatch()` / `AbstractProcessor` | Async read/write/error events re-enter container processing with explicit error/recycling rules | Keep semantic state machine in Java; native layer emits transport events |
| Response framing | `Http11Processor.prepareResponse()` / `Http11OutputBuffer` | Response framing selects filters and connection handling before commit | Keep response policy in Java initially; native layer may later own transport writes |
| Keep-alive recycle | `Http11InputBuffer.nextRequest()` / `Http11OutputBuffer.nextRequest()` / `CoyoteAdapter` | Request/response state and filters are explicitly recycled between requests | Java remains request-state owner; native connection lifetime is independent |

## Verified source relationship

```text
NioEndpoint
  acceptor
    -> socket setup / Poller registration
      -> SocketProcessor / Processor
        -> Http11Processor
          -> Http11InputBuffer / Http11OutputBuffer
            -> CoyoteAdapter
              -> Catalina pipeline
                -> FilterChain
                  -> Servlet
```

`CoyoteAdapter` is a semantic boundary, not a reason to reproduce Tomcat objects in C. `AbstractProcessor` also contains async/error state that must not be recreated as an unverified native approximation.

## Initial native boundary

```text
C native
  listener / epoll event loop
      -> native connection / socket state
      -> readiness and transport I/O
      -> JNI bulk bridge

Java/Tomcat
  SocketWrapperBase / NioSocketWrapper-compatible transport surface
      -> Http11InputBuffer / Http11OutputBuffer
      -> Http11Processor
      -> CoyoteAdapter
      -> Catalina / mapping / FilterChain / Servlet
      -> async lifecycle and application semantics

C native
  transport write
      -> readiness
      -> socket
```

The first phase deliberately does **not** move HTTP parsing or Tomcat-owned parser buffers into C. Native code may borrow a Java/Tomcat direct `ByteBuffer` only for the duration of a JNI call; it must not retain its address or free/recycle the buffer.

The native layer also must not expose the native file descriptor directly to Servlet/application code. Native connection lifetime, Java request lifetime and Servlet async lifetime are distinct state machines.

## Ownership requirements

For every native/Java field or handle, the implementation must specify:

- representation;
- owner;
- lifetime;
- mutability;
- thread affinity;
- synchronization;
- copy/no-copy status;
- error/cancellation behaviour.

In particular, native code must never free or recycle memory while Java `ByteBuffer`/Servlet code can still observe it.

## NGINX reference

NGINX 1.30.4 source inspection confirms an event-driven architecture with an event core, platform event modules and posted-event queues. The design lesson is event-loop separation and deferred work, not wholesale copying of NGINX data structures.

Relevant paths:

- `src/event/ngx_event.c`
- `src/event/ngx_event_posted.c`

## Servlet 6.1 boundary

Tomcat 11.0.25 implements Servlet 6.1. Servlet compatibility therefore cannot be inferred from TCP acceptance or HTTP parsing alone.

Java/Tomcat continues to own at least:

- Servlet lifecycle;
- application class loading/isolation;
- Filter and Listener execution;
- URL/context/wrapper mapping;
- application-facing Request/Response semantics;
- Servlet async lifecycle and callbacks;
- application exception/error semantics;
- JSP/Jasper integration.

## Non-decisions

- No claim that native transport is faster than Tomcat's Java implementation.
- No claim that JNI is faster than FFM; JNI is the initial compatibility-oriented bridge.
- No claim that the current native runtime is Servlet-compatible.
- No benchmark result is recorded here.
