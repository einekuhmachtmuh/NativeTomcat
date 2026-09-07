# NativeEndpoint Responsibility Matrix

本文件是 `docs/work-principles.md` 所要求的 source-first implementation gate。它把 pinned Tomcat 11.0.25 的 endpoint contract 與 NativeTomcat 的 native event ownership 分開，作為建立 `NativeEndpoint.java` 前的最後設計核驗文件。

## 1. Source baseline

- Tomcat pinned commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`
- `AbstractEndpoint.java`: `85f1c9e23464e17ae14108ae4b04fbf4dad07cdd`
- `AbstractNetworkChannelEndpoint.java`: `d1cc46d562515ae502b4a3106cd478ad235351ef`
- `NioEndpoint.java`: `21b0cadbbb3ab04351415c0d7c127ab3500ace58`

## 2. Responsibility matrix

| Concern | Tomcat NIO reference | NativeEndpoint | Owner in NativeTomcat |
|---|---|---|---|
| Endpoint lifecycle | `AbstractEndpoint.init/start/stop/destroy` | preserve inherited lifecycle | Java endpoint |
| Server bind | `NioEndpoint.bind/initServerSocket` | bind native listener/runtime | native runtime, invoked by endpoint |
| Accept | `Acceptor` + `serverSocketAccept()` | native runtime accepts | native event loop |
| Connection identity | `connections` keyed by underlying socket/channel | stable `uint64` handle -> wrapper | Java registry + native handle registry |
| Readiness polling | `Poller` + `Selector` | epoll | native event loop |
| Ready-event dispatch | `Poller.processKey()` | handle -> wrapper -> `processSocket()` | native event loop + Java endpoint |
| Read/write event mapping | `OPEN_READ` / `OPEN_WRITE` | same `SocketEvent` values | Java endpoint boundary |
| Processor creation | `createSocketProcessor()` | real `SocketProcessorBase` subclass | Java endpoint |
| Executor dispatch | `AbstractEndpoint.processSocket()` | reuse inherited method | Java endpoint |
| Socket wrapper state | `NioSocketWrapper` | `NativeSocketWrapper` | Java |
| fd read/write | `NioChannel` / Java channel | future native transport bridge | native transport + Java wrapper |
| Interest/rearm | `SelectionKey.interestOps()` / Poller | `epoll_ctl` / rearm | native event loop |
| Close | wrapper/channel close + endpoint registry | native close plus wrapper lifecycle | coordinated native/Java ownership |
| Timeout | `Poller.timeout()` | native/runtime timeout source, later mapped | not implemented in first gate |
| TLS | `NioChannel` / JSSE/OpenSSL integration | deferred | later |
| sendfile | `NioEndpoint.processSendfile()` | deferred | later |
| vectored I/O | `SocketWrapperBase`/NIO transport | deferred | later |

## 3. Critical invariants

### 3.1 No dual event owner

A native connection must not be registered in both native epoll and Java `Selector`. `NioEndpoint.Poller` is therefore a semantic reference only; its selector implementation is not reused by `NativeEndpoint`.

### 3.2 Readiness is not protocol processing

The native event callback only identifies a ready native connection. It must not parse HTTP or call Servlet code directly. The Java boundary is:

```text
native epoll event
    -> stable handle
    -> NativeEndpoint resolves NativeSocketWrapper
    -> AbstractEndpoint.processSocket(wrapper, event, true)
    -> SocketProcessorBase
    -> protocol handler
```

### 3.3 Rearm belongs to the native owner

The Java processor may consume data and eventually request another read/write interest, but it must not directly mutate the epoll registration. A later bridge must marshal such requests to the native event-loop owner.

This follows the same separation visible in NGINX: `ngx_process_events` drives event delivery while `ngx_handle_read_event()` / `ngx_handle_write_event()` update event state. NGINX's epoll backend uses separate read/write event registration rather than making application handlers the kernel-event owner.

### 3.4 `processSocket()` remains the Tomcat dispatch boundary

`AbstractEndpoint.processSocket()` must not be duplicated in native code. It owns processor-cache reuse, `createSocketProcessor()`, executor selection, rejection handling and invocation of `SocketProcessorBase`.

### 3.5 First NativeEndpoint gate is intentionally incomplete

The first implementation must establish only the real endpoint/processor boundary. It must not pretend that native read/write, `isReady()`, TLS, sendfile or Servlet integration are complete.

## 4. NativeEndpoint implementation shape

The first implementation should extend `AbstractEndpoint<NativeSocketWrapper, NativeConnection>` only if the exact source audit confirms that the native connection type can satisfy the `U` contract without introducing a fake channel abstraction.

It must implement the verified abstract methods:

```text
bind()
startInternal()
stopInternal()
getLog()
createSocketProcessor()
doCloseServerSocket()
serverSocketAccept()
setSocketOptions()
destroySocket()
```

It should not extend `AbstractNetworkChannelEndpoint` merely for convenience: that class specifically requires `U extends NetworkChannel` and implements `getLocalAddress()` through a Java `NetworkChannel`. The current native connection is an fd/handle abstraction, not a Java `NetworkChannel`.

## 5. NGINX cross-check

Official NGINX event source confirms the relevant separation:

```text
kernel event backend
    -> ngx_process_events_and_timers()
    -> event handler
    -> ngx_handle_read_event()/ngx_handle_write_event()
```

For epoll, NGINX defines read readiness as `EPOLLIN|EPOLLRDHUP` and write readiness as `EPOLLOUT`; event state is maintained by the event layer rather than by the protocol handler itself.

NativeTomcat therefore keeps epoll ownership in `nt_runtime`, while Java owns Tomcat wrapper/protocol semantics.

## 6. Implementation gate

Before writing `NativeEndpoint.java`, the remaining source question is no longer the abstract-method list; it is the exact type/lifecycle contract for the native connection object and how a wrapper is inserted into `AbstractEndpoint.connections` without creating a fake `NetworkChannel`.

The next implementation must therefore first inspect the `connections` field's generic usage, `SocketWrapperBase` close/reset semantics, and the existing `NativeSocketWrapper` constructor/state. Only after that inspection may `NativeEndpoint.java` be created.

## 7. Verification status

- pinned Tomcat source: verified for the cited files
- NIO selector/Poller ownership: source-verified
- NGINX event ownership: cross-checked against official source
- responsibility matrix: recorded
- `NativeEndpoint.java`: not yet created
- current latest repo: not freshly compiled in this environment
- unit/integration/TCK/benchmark: not verified for this stage
