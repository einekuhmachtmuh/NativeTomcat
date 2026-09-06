# Upstream source mapping

This document records source-level facts verified against Apache Tomcat 11.0.25 and NGINX 1.30.4. It is intentionally limited to inspected paths and does not claim to be a complete implementation inventory.

## Tomcat network and request path

| Stage | Upstream implementation | Verified fact | NativeTomcat decision |
|---|---|---|---|
| Connector construction | `java/org/apache/catalina/connector/Connector.java` | The default `Connector()` selects HTTP/1.1 and protocol creation is delegated to `ProtocolHandler.create()` | Keep Connector/Catalina lifecycle in Java |
| Protocol handler | `java/org/apache/coyote/AbstractProtocol.java`, `AbstractHttp11Protocol.java` | `AbstractProtocol` owns an `AbstractEndpoint`; an `Adapter` links protocol processing to the container | Preserve the Java Adapter boundary initially |
| Endpoint lifecycle | `java/org/apache/tomcat/util/net/AbstractEndpoint.java`, `NioEndpoint.java` | Endpoint state includes running/paused state, an acceptor, connection tracking, processor/event caches and an executor | Native endpoint may replace low-level I/O only after equivalent lifecycle semantics are specified |
| Accept | `NioEndpoint.serverSocketAccept()` and `Acceptor.run()` | The acceptor accepts a socket, checks endpoint state and hands the socket to `setSocketOptions()` | C accept/event-loop candidate |
| Connection registration | `NioEndpoint.setSocketOptions()` | A channel/wrapper is created or reused, socket properties and timeouts are configured, and the socket is registered with the Poller | C connection state must explicitly own equivalent state |
| Poller | `NioEndpoint.Poller` | Registration events are queued; the selector is woken when necessary; readable/writable keys are processed and dispatched as socket events | Strong native candidate; Java event semantics remain above the boundary |
| Socket wrapper | `SocketWrapperBase` / `NioSocketWrapper` | The wrapper owns socket buffers, timeouts, keep-alive state, error state, processor association and non-blocking read/write state | Do not translate field-for-field; define a smaller native connection state with explicit ownership/lifetime |
| HTTP request line | `Http11InputBuffer.parseRequestLine()` | Parsing is incremental and stateful and can return before completion when more bytes are required | Native parser must be incremental, bounded and resumable |
| HTTP headers/body framing | `Http11InputBuffer`, `Http11Processor.prepareInputFilters()` | Header parsing and transfer/content-length decisions feed an input-filter chain; malformed framing changes response/error/keep-alive state | Native parser may produce a validated wire representation, but Servlet-visible framing semantics must remain equivalent |
| Request preparation | `Http11Processor.prepareRequest()` | It validates Host, URI, transfer encoding, content length, SNI and related protocol state | Security-critical; native implementation requires differential tests against Tomcat |
| Servlet dispatch | `CoyoteAdapter.service()` | It creates/links Catalina Request/Response objects, performs request preparation and invokes the container pipeline | Keep in Java |
| Filter/Servlet execution | `StandardWrapperValve.invoke()` | It allocates the servlet, creates the application filter chain and invokes `filterChain.doFilter()` | Keep in Java |
| Async dispatch | `CoyoteAdapter.asyncDispatch()` / `AbstractProcessor` | Async read/write/error events re-enter container processing and have explicit error and recycling rules | Keep semantic state machine in Java; native layer emits events |
| Response framing | `Http11Processor.prepareResponse()` / `Http11OutputBuffer` | Response framing chooses identity/chunked/void filters, compression, Connection handling and headers before commit | Keep response policy in Java initially; native layer may own final buffered write path later |
| Recycle | `Http11InputBuffer.nextRequest()` / `Http11OutputBuffer.nextRequest()` / `CoyoteAdapter` | Request/response and filters are explicitly recycled between keep-alive requests | Native connection buffers need an explicit per-request reset boundary |

## Verified Tomcat source relationships

The source path is not simply `socket -> servlet`:

```text
NioEndpoint
  acceptor
    -> setSocketOptions
      -> NioSocketWrapper / Poller registration
        -> SocketProcessor / Processor
          -> Http11Processor
            -> Http11InputBuffer / Http11OutputBuffer
              -> CoyoteAdapter
                -> Catalina pipeline
                  -> FilterChain
                    -> Servlet
```

`CoyoteAdapter` is therefore a semantic boundary rather than a convenient place to translate every object to C. `AbstractProcessor` also contains async state and error handling that must not be recreated as an unverified native approximation.

## Request/response ownership model to implement

The next native design must explicitly specify, for every native/Java field:

- representation;
- owner;
- lifetime;
- mutability;
- thread affinity;
- synchronization;
- copy/no-copy status;
- error/cancellation behaviour.

In particular, the native layer must not free or recycle a buffer while Java `ByteBuffer`/Servlet code can still observe it.

## NGINX reference

NGINX 1.30.4 source inspection confirms an event-driven architecture with an event core, platform event modules and posted-event queues. `ngx_event_process_posted()` drains queued handlers, while the official development guide describes posted events as deferred work within the event-loop iteration after I/O and timer processing.

Relevant paths:

- `src/event/ngx_event.c`
- `src/event/ngx_event_posted.c`

The design lesson is event-loop separation and explicit deferred work, not copying NGINX data structures wholesale.

## Servlet 6.1 boundary

Tomcat 11.0.25 documents and exposes Servlet 6.1. The Servlet API defines the application/container contract and Servlet lifecycle. Consequently, accepting TCP and parsing HTTP is not evidence of Servlet compatibility.

The Java side must continue to own at least:

- Servlet lifecycle;
- application class loading/isolation;
- Filter and Listener execution;
- URL/context/wrapper mapping;
- application-facing Request/Response semantics;
- Servlet async lifecycle and callbacks;
- application exception/error semantics;
- JSP/Jasper integration.

## Current native boundary

```text
C native
  listener / event loop
      -> native connection state
      -> incremental HTTP wire parsing
      -> bounded input/output buffers
      -> readiness events
      -> bulk bridge

Java/Tomcat
  Coyote request/response
      -> Connector / Adapter
      -> mapping / Context / Wrapper
      -> FilterChain
      -> Servlet
      -> async lifecycle / application semantics

C native
  response bytes
      -> buffered write / readiness
      -> socket
```

This boundary remains provisional. Profiling and differential testing can move the boundary later.

## Non-decisions

- No claim that native HTTP parsing is faster than Tomcat's Java parser.
- No claim that JNI is the preferred interop mechanism; FFM and other bulk-transfer designs remain open.
- No claim that the current native runtime is Servlet-compatible.
- No benchmark result is recorded here.
