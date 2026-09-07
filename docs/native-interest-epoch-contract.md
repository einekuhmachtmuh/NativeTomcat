# Native interest / readiness epoch contract

## 1. Status

本文件是 NativeTomcat 在進入 HTTP/1.1 real integration 前，對 native transport interest/rearm 語意的強制 contract。

證據基準：

- NativeTomcat `main`：工作開始時核實的 HEAD `d8ef42deceb00f645b4e5e979ccb113e820f0322`。
- Apache Tomcat：11.0.25，pinned `cbe6e15ee81e2fc6232954292a80cca5d1e84009`。
- NGINX：本次以官方 `master` 的 event/epoll source 作 architecture cross-check；未宣稱其等同 NativeTomcat，也未以未核驗的 NGINX 1.30.4 source 作版本證據。

本文件不宣稱目前實作已符合全部 contract；它定義下一個 implementation gate 的必要語意。

## 2. Source-derived requirements

### 2.1 Tomcat interest is a set, not a boolean

Pinned Tomcat `NioEndpoint.Poller.events()` merges a newly requested interest with the existing key interest using bitwise OR. `processKey()` first removes the readiness bits that were delivered, then dispatches READ before WRITE. Therefore READ and WRITE must be represented independently.

NativeTomcat must therefore represent at least:

```text
READ  = 0x1
WRITE = 0x2
```

The exact native epoll flags are an implementation detail; the Java-side contract is an independent READ/WRITE desired-interest set.

### 2.2 Read registration must preserve write registration

If desired interest is `WRITE` and Java later calls `registerReadInterest()`, the resulting desired set is `READ | WRITE`, not `READ`.

Likewise, `registerWriteInterest()` must preserve an existing READ request.

A boolean `want_write` cannot represent this contract.

### 2.3 Readiness consumes only the delivered direction

At the beginning of a native readiness epoch, the delivered readiness bits are consumed from the desired-interest set in the same conceptual manner as Tomcat's `unreg()`:

```text
newDesired = oldDesired & ~readyBits
```

This means a READ readiness event clears READ from the pending interest but does not clear WRITE. A WRITE readiness event clears WRITE but does not clear READ. An error/close path may terminate the connection instead of preserving either bit.

This reset is required for `EPOLLONESHOT`: the previous readiness notification must not remain represented as an already-active interest after the event has been consumed.

### 2.4 Native event-loop ownership

Only the native event-loop owner may perform the final `epoll_ctl()` and native socket close.

Java/Tomcat may mutate the connection's desired-interest state and enqueue a request to the native owner, but must not call `epoll_ctl()` or `close(fd)` directly.

### 2.5 Native apply operation

The native apply operation must consume a complete desired-interest mask for one connection rather than a boolean WRITE flag.

Conceptually:

```text
apply(handle, desiredMask)
```

where `desiredMask` is a complete snapshot of READ/WRITE desired interest for that connection.

`desiredMask == 0` means that no readiness event is currently requested; it does not mean that WRITE is disabled while READ remains enabled.

### 2.6 Close dominates rearm

For a connection that has entered the closed state, a later rearm request is stale and must not reactivate the descriptor.

If CLOSE and REARM requests are coalesced in the same native command batch, CLOSE dominates.

This is consistent with the existing NativeTomcat command-queue rule and with the requirement that closed/stale handles cannot revive a transport.

### 2.7 Stale handles

A native handle is valid only while its registry entry is active. A command for an inactive/stale handle must become a safe no-op or a reported stale-command result; it must never dereference a destroyed connection.

The existing monotonic stable-handle design remains the identity mechanism. This contract does not by itself prove object lifetime safety between `nt_runtime_find_connection()` returning a pointer and another thread destroying that object; that remains a separate lifetime gate.

## 3. Epoch definition

A **readiness epoch** is the interval beginning when the native owner consumes one readiness notification for a connection and ending when the corresponding Java/Tomcat processing has reached an explicit transport-completion point, or the connection has been closed.

For an ordinary non-blocking SocketProcessor path:

```text
kernel readiness
    -> native event owner consumes ready bits
    -> Java wrapper consumes those ready bits from desired interest
    -> Tomcat processSocket / SocketProcessorBase
    -> protocol/application transport consumption
    -> Java wrapper records final desired READ/WRITE state
    -> one transport completion request
    -> native owner applies final desired mask OR closes
```

Important: command-queue batching is not the definition of an epoch and is not proof of exactly-one application.

## 4. Exactly-one rule

For each ordinary readiness epoch, NativeTomcat must have exactly one **effective transport completion decision**:

```text
REARM(finalDesiredMask)
OR
CLOSE
```

This is stronger than "the command queue coalesced several commands".

The implementation must make the completion boundary explicit. The native event loop must not wait synchronously for the Java Executor or `SocketProcessorBase` to finish merely to manufacture this property.

### 4.1 Blocking waiter exception

A blocking read/write waiter is a separate transport-consumption path. If a blocking read or write would block, it may immediately request interest for the corresponding direction so the native owner can wake the waiter.

This immediate waiter rearm must not be suppressed by the ordinary processor completion mechanism.

If a readiness epoch wakes a blocking waiter, the waiter owns the corresponding transport-consumption continuation; the implementation must prevent a second independent processor for that same direction from racing it.

## 5. Java-side state ownership

The wrapper owns the logical desired-interest state on the Tomcat side.

Required properties:

1. READ and WRITE are independent bits.
2. Updates are synchronized with readiness consumption and with close.
3. `registerReadInterest()` performs `desired |= READ`.
4. `registerWriteInterest()` performs `desired |= WRITE`.
5. Readiness consumption performs `desired &= ~readyBits` before normal processor dispatch.
6. CLOSE makes the wrapper permanently closed and prevents future interest publication.
7. The final completion operation publishes the complete desired mask, not a delta represented by a boolean.

The exact lock/atomic implementation is an implementation choice, but it must preserve these ordering properties.

## 6. Native-side state ownership

The native runtime remains the sole owner of:

- epoll registration;
- native file descriptor close;
- final application of the complete READ/WRITE mask;
- rejection of stale handles;
- native connection state transition.

The native runtime may suppress an `epoll_ctl()` when the requested mask is already the currently applied mask. Such a no-op optimization is valid only after the applied-mask state is itself correctly synchronized with readiness consumption and close.

Therefore:

```text
command coalescing != applied-state deduplication != exactly-one epoch completion
```

These are three different properties and must be tested separately.

## 7. Tomcat cross-check

Pinned Tomcat 11.0.25 establishes the following relevant behaviour:

- `NioEndpoint.Poller.processKey()` checks READ and WRITE independently and processes READ before WRITE.
- `unreg()` removes only the delivered ready operations from the key's interest set.
- `events()` applies new registration using `key.interestOps() | interestOps`.
- `NioSocketWrapper` retains an `interestOps` representation used by timeout and interest checks.
- `SocketWrapperBase.registerReadInterest()` and `registerWriteInterest()` are abstract transport operations; the transport implementation therefore has to preserve the above set semantics.
- `SocketProcessorBase.run()` provides the per-wrapper processor lock; the native implementation must not bypass it.

NativeTomcat therefore adopts the semantic model of an independently represented READ/WRITE desired set, while keeping epoll ownership native.

## 8. NGINX cross-check

Official NGINX event/epoll source provides an independent native-event architecture reference:

- `ngx_handle_read_event()` and `ngx_handle_write_event()` are separate event-registration operations.
- The epoll backend's `ngx_epoll_add_event()` preserves the opposite-direction event when adding the requested direction; its `events |= prev` logic is direct evidence that READ and WRITE registration are not a single boolean state.
- `ngx_epoll_del_event()` similarly retains the opposite active direction when removing one side.
- `ngx_epoll_process_events()` handles EPOLLIN and EPOLLOUT separately and checks for stale events before dispatch.
- NGINX's current epoll backend uses clear/edge-triggered event handling rather than serving as evidence that NativeTomcat should use `EPOLLONESHOT`.

NGINX therefore supports the architectural separation and independent direction model, but it does not define Tomcat's Servlet or SocketWrapper semantics.

## 9. Required implementation gate

Before real HTTP integration, the implementation must be changed and re-verified so that:

1. `NativeTransport.rearm()` accepts a complete interest mask rather than `boolean wantWrite`.
2. `NativeSocketWrapper` stores independent READ/WRITE desired state.
3. Read/write registration is additive.
4. Native readiness consumes only the delivered bits.
5. Blocking waiter rearm remains immediate and direction-specific.
6. The ordinary SocketProcessor path has an explicit completion boundary capable of producing one final REARM(mask) or CLOSE decision.
7. Native command coalescing uses complete desired masks and CLOSE dominance.
8. Native owner tracks enough applied state to distinguish an actual epoll change from a no-op.
9. Close/stale-handle behaviour is source-verified for every transition.
10. The implementation is then compiled and unit-tested before HTTP integration is attempted.

No benchmark or Servlet/TCK claim may be made from this contract alone.
