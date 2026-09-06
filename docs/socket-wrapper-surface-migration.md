# SocketWrapperBase minimum concrete surface

## Verification status

This document marks the current implementation as a **surface proof**, not as a claim that NativeTomcat has replaced Tomcat 11.0.25's transport implementation.

The reference is the pinned Tomcat 11.0.25 source commit:

`cbe6e15ee81e2fc6232954292a80cca5d1e84009`

Primary source files checked before creating this surface:

- `org/apache/tomcat/util/net/SocketWrapperBase.java`
- `org/apache/tomcat/util/net/NioEndpoint.java`
- `org/apache/tomcat/util/net/NioEndpoint.NioSocketWrapper`
- `org/apache/tomcat/util/net/SocketProcessorBase.java`

The Tomcat API documentation also confirms that `NioEndpoint.NioSocketWrapper` directly subclasses `SocketWrapperBase<NioChannel>` and that `SocketProcessorBase.run()` is the processor execution boundary.

## Surface established in this repository

The first repository-owned surface is:

```text
SocketWrapperBase<E>
    |
    +-- NativeSocketWrapper
    |
    +-- SocketProcessorBase<S>
            |
            +-- NativeSocketProcessor
```

The following surface categories are deliberately represented:

| Category | Current surface | Status |
|---|---|---|
| socket identity | `getSocket()` / opaque native handle | present |
| endpoint association | `getEndpoint()` | present |
| per-connection lock | `getLock()` | present |
| processor association | `getCurrentProcessor()` / `setCurrentProcessor()` / `takeCurrentProcessor()` | present |
| close state | `close()` / `isClosed()` / `doClose()` | present |
| read | byte[] + `ByteBuffer` overloads | explicit deferred implementation |
| read readiness | `isReadyForRead()` | present as transport-surface proof |
| application read buffer | `setAppReadBufHandler()` | surface present |
| write | byte[] + `ByteBuffer` overloads | explicit deferred implementation |
| write readiness | `canWrite()` / `isReadyForWrite()` | surface present |
| write interest | `registerWriteInterest()` | present |
| read interest | `registerReadInterest()` | present |
| flush | `flush()` / `flushNonBlocking()` | surface present |
| timeout | read/write timeout getters/setters | present |
| keep-alive | setter/decrement operation | present |
| addresses | local/remote getters + population hooks | present |
| negotiated protocol/SNI | getters/setters | present |
| error state | `getError()` / `setError()` / `checkError()` | present |
| sendfile | create/process surface | explicitly deferred |
| TLS | client auth / SSL support surface | explicitly deferred |
| vectored async I/O | operation/enums/callback surface | explicitly deferred |
| push-back | `unRead()` | explicitly deferred |
| SocketProcessor dispatch | `processSocket()` | connected to endpoint surface |

## SocketProcessorBase verification

Tomcat 11.0.25's `SocketProcessorBase` has two critical properties that are preserved here:

1. it owns a `SocketWrapperBase<S>` and a `SocketEvent`;
2. `run()` acquires the wrapper's lock before invoking `doRun()` and releases it afterwards.

`NativeSocketProcessor` exists only to prove this serialization boundary. The current test submits multiple processors for one wrapper and requires `maxActive == 1`.

This is intentionally different from the native event loop: native readiness detection may be concurrent with other native work, but Java protocol processing for one logical connection remains serialized.

## NioEndpoint / NioSocketWrapper comparison

The Tomcat 11.0.25 NIO implementation establishes the following reference model:

```text
NioEndpoint
  -> NioSocketWrapper extends SocketWrapperBase<NioChannel>
  -> Poller interest/ready handling
  -> AbstractEndpoint.processSocket(...)
  -> SocketProcessorBase
```

The NativeTomcat surface currently mirrors the abstraction boundary, but it does **not** claim to implement NioEndpoint's selector/poller, sendfile, TLS, or NIO2 behavior.

In particular, the current `NativeSocketWrapper` does not turn native epoll readiness into Servlet readiness. That distinction remains mandatory.

## NGINX cross-check

The native event design was also checked against NGINX's event architecture. `ngx_event_accept.c` accepts connections and initializes connection/event state; `ngx_event.c` separates event polling from event handling and explicitly manages read/write event registration. NGINX's event flags distinguish level, one-shot and clear-event semantics.

This supports the NativeTomcat rule that:

```text
native readiness
    != Java SocketProcessor execution
    != Servlet isReady()
```

The native runtime therefore remains responsible for readiness and re-arm policy, while the Java layer owns protocol/Servlet semantics.

## What is deliberately NOT claimed yet

The following are not complete:

- exact upstream `SocketWrapperBase` implementation body;
- exact `NioSocketWrapper` implementation body;
- exact `NioEndpoint` implementation body;
- actual native handle read/write JNI methods;
- actual `AbstractEndpoint.processSocket()` implementation;
- actual Tomcat `SocketProcessor` reuse/pooling;
- HTTP/1.1 `Http11Processor` entry;
- TLS/OpenSSL;
- sendfile;
- vectored async I/O;
- protocol upgrade.

These remain separate gates so that a successful surface test cannot be mistaken for a complete Tomcat transport integration.

## Next implementation gate

The next step is to replace the compatibility shell incrementally with the corresponding Tomcat 11.0.25 source while preserving the passing surface test. Each replacement must be checked against the pinned source before the next class is introduced.
