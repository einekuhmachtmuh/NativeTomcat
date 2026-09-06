# Native connection event contract

## 1. Scope and verification status

The current native transport layer uses Linux `epoll`. Listener and wakeup monitoring are level-triggered; accepted connections use `EPOLLONESHOT`.

This document defines the boundary between kernel readiness and the NativeTomcat event dispatcher. It does not define Servlet readiness, HTTP parsing, or complete Tomcat transport semantics.

Status terms used here:

- **implemented** — present in repository code;
- **source-verified** — checked against the pinned external source/specification;
- **tested** — exercised by an executable test;
- **deferred** — intentionally not implemented;
- **blocked** — required verification cannot currently be executed in the local environment.

## 2. Registration contract

Accepted sockets are registered with:

- `EPOLLIN`;
- `EPOLLRDHUP`;
- `EPOLLONESHOT`.

`EPOLLOUT` is not a permanent interest bit. It is intended to be armed only when the transport layer has pending output that currently requires write readiness.

The runtime, not Java code, owns epoll registration and rearm operations.

## 3. Why EPOLLONESHOT is used

At the current stage there is no native HTTP parser or completed Java transport-consumption path. Ordinary level-triggered `EPOLLIN` could repeatedly report an unread socket and produce a busy loop.

`EPOLLONESHOT` makes each readiness delivery a one-shot handoff. After notification, the connection remains disabled until an explicit `EPOLL_CTL_MOD` rearm.

The important ownership distinction is:

```text
readiness notification != data consumption != Java task submission
```

Rearm must correspond to completion of the event-processing contract, not merely to successful queuing of Java work.

## 4. Event translation

The native callback exposes an event mask:

| Native condition | Runtime event |
|---|---|
| `EPOLLIN` | `NT_RUNTIME_EVENT_READABLE` |
| `EPOLLOUT` | `NT_RUNTIME_EVENT_WRITABLE` |
| `EPOLLRDHUP` / `EPOLLHUP` | `NT_RUNTIME_EVENT_PEER_READ_CLOSED` |
| `EPOLLERR` | `NT_RUNTIME_EVENT_ERROR` |

These are transport events. They are not `SocketEvent` values and are not Servlet listener callbacks.

Mapping to Tomcat `SocketEvent` must occur only after the real `SocketWrapperBase`/`AbstractEndpoint.processSocket()` integration has been established.

## 5. Native callback boundary

The callback executes on the native event-loop thread. At this boundary the native runtime owns the current readiness notification and remains responsible for native registration state.

The callback may perform non-blocking native work when that work is part of the established native event-processing contract. It must not assume that Java execution has already consumed the socket merely because JNI returned successfully.

## 6. Current JNI hand-off

The current path is:

```text
native epoll thread
    -> JNI attach
    -> NativeTomcatBootstrap.dispatchNativeEvent()
    -> NativeEventDispatcher
    -> Tomcat connector Executor
    -> queued Java work
```

`NativeEventDispatcher` coalesces event masks for one native handle and serializes Java tasks for that handle.

This is a concurrency boundary, not yet a Tomcat transport boundary. The queued task currently stops before `SocketWrapperBase`, `AbstractEndpoint.processSocket()`, `SocketProcessorBase`, and `Http11Processor`.

## 7. Critical current mismatch

The design contract requires rearm after the consumer has completed the current event-processing step. The current native callback can instead rearm immediately after JNI dispatch, while `NativeEventDispatcher` has only queued Java work.

Therefore the repository currently has a **known ownership gap**:

```text
current implementation:
  epoll event -> JNI enqueue -> immediate rearm

required integrated model:
  epoll event -> ownership transfer/dispatch
             -> actual transport/Tomcat consumption
             -> next interest set
             -> exactly one rearm by native event-loop owner
```

This mismatch must be fixed before the event contract is treated as an implemented back-pressure mechanism. It is a documentation/code alignment issue, not evidence that Tomcat integration is already working.

## 8. Write-interest rule

`EPOLLOUT` must be armed only when a real transport write queue contains bytes that could not currently be written without blocking.

Receiving an `EPOLLOUT` event is not itself a reason to keep `EPOLLOUT` armed. Once pending output has been drained, write interest must be removed from the next registration state.

This avoids the classic writable-socket busy loop.

## 9. Readiness layering

The project must preserve three distinct layers:

```text
Linux kernel readiness
        |
        v
Native event handling / rearm
        |
        v
Tomcat socket transport + processor state
        |
        v
Servlet non-blocking readiness and listener callbacks
```

Thus:

`EPOLLIN != SocketEvent.OPEN_READ != ServletInputStream.isReady()`

and

`EPOLLOUT != SocketEvent.OPEN_WRITE != ServletOutputStream.isReady()`.

A native event may cause Java to attempt processing, but only Tomcat/Servlet state determines the application-visible result.

## 10. Cross-thread rule

A Java worker thread must not directly perform `epoll_ctl` against event-loop-owned registration as a shortcut.

If Java needs a change to native interest, close, or another event-loop-owned operation, the request must be marshalled to the native event-loop owner through an explicit queue/wakeup mechanism. The ordering and acknowledgement protocol are deferred until the JNI transport bridge is designed.

## 11. Failure and terminal events

Error, peer half-close, and full close must be represented separately until the Tomcat protocol layer establishes the correct response/keep-alive behavior.

The native layer must not emit duplicate terminal callbacks merely because multiple epoll flags are present on one notification.

The final exactly-once close propagation rule is deferred until native-handle lifetime is connected to the Java wrapper lifetime.

## 12. NGINX cross-check

NGINX is used only as an architectural cross-check. Its event subsystem separates event polling, read/write event registration and handler dispatch, and distinguishes level/one-shot/clear-event modes.

This supports the NativeTomcat separation of readiness detection, native event handling, and higher-level processing. It does not establish Tomcat or Servlet requirements.

## 13. Next gate

Before the next transport integration step:

1. resolve the `EPOLLONESHOT` rearm ownership gap;
2. define native read/write/close/rearm operations required by the Java adapter;
3. map native handles to a stable Java transport object and lifetime;
4. connect the object to the real Tomcat `processSocket()` / `SocketProcessor` path;
5. test event coalescing, serialization, partial I/O, close/error, and rearm behavior.

Only then should `Http11Processor` become an integration target.
