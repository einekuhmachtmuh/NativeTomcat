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

## 7. Current EPOLLONESHOT and close state

The previous native callback rearmed immediately after `nt_jvm_dispatch_event()`. That was inconsistent with this contract because JNI dispatch only queues Java work.

The implementation has now been corrected in two ways:

1. it **does not rearm after asynchronous Java dispatch**;
2. it **does not close a peer-half-closed/error connection immediately after dispatch**, because the queued Java task has not yet consumed the terminal transport event.

The only immediate close at this boundary is failure to dispatch the event into Java. Otherwise the connection remains owned by the runtime and one-shot disabled until a real transport/Tomcat consumer determines the next action.

Consequently the current safe boundary is:

```text
normal event:
  epoll event -> JNI enqueue -> no rearm

terminal event:
  epoll event -> JNI enqueue -> no rearm / no immediate close

required integrated model:
  epoll event -> ownership transfer/dispatch
             -> actual transport/Tomcat consumption
             -> next interest/close decision
             -> exactly one native-owner action
```

This is intentionally incomplete. It prevents the current asynchronous Java boundary from pretending that event submission equals transport consumption or connection completion.

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

At the current asynchronous boundary, native code does not infer final connection destruction from the event callback's return. Final close/recycle ownership is deferred to the transport/Tomcat integration layer, except when Java event dispatch itself fails.

## 12. NGINX cross-check

NGINX is used only as an architectural cross-check. Its event subsystem separates event polling, read/write event registration and handler dispatch, and distinguishes level/one-shot/clear-event modes.

This supports the NativeTomcat separation of readiness detection, native event handling, and higher-level processing. It does not establish Tomcat or Servlet requirements.

## 13. Next gate

Before the next transport integration step:

1. implement the actual native transport-consumption path;
2. define how completion requests the next interest set or close from the native event-loop owner;
3. map native handles to a stable Java transport object and lifetime;
4. connect the object to the real Tomcat `processSocket()` / `SocketProcessor` path;
5. test event coalescing, serialization, partial I/O, close/error, and exactly-once rearm/close behavior.

Only then should `Http11Processor` become an integration target.
