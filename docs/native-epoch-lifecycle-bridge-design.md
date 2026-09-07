# Native readiness epoch lifecycle bridge design

## Status

`theoretical analysis / source-verified design`; not yet implemented.

## Evidence that constrains the design

- Pinned Apache Tomcat 11.0.25: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`.
- `NioEndpoint.Poller.processKey()` consumes the current `readyOps()` first, then independently processes READ and WRITE. Each direction may call `processSocket()` separately.
- `AbstractEndpoint.processSocket()` creates/resets a `SocketProcessorBase`, submits it to `getExecutor()` when `dispatch=true`, and returns `true` only after submission succeeds.
- `SocketProcessorBase.run()` is `final`; it locks the wrapper, checks `isClosed()`, and may return before `doRun()`. Therefore completion cannot be implemented only in `doRun().finally`.
- NativeTomcat's current native runtime uses `EPOLLONESHOT`; its native event-loop owner applies the final epoll interest mask.
- NGINX's official event layer keeps READ/WRITE state independent and does not establish the Java executor completion boundary required here. NGINX is therefore an architecture cross-check, not a lifecycle implementation source.

## Candidate bridge

### 1. Keep Tomcat's real `processSocket()` path

Do not replace `AbstractEndpoint.processSocket()` with a parallel submission mechanism. `NativeEndpoint.processNativeEvent()` should continue to invoke the inherited `processSocket()` so the pinned `SocketProcessorBase` contract remains the dispatch boundary.

### 2. Introduce an explicit native-readiness epoch token

Each native readiness notification creates a monotonically increasing per-connection epoch token containing:

- epoch sequence;
- consumed READ/WRITE readiness bits;
- expected processor count;
- completed processor count;
- cancelled/closed state;
- finalization state.

READ and WRITE remain independent. A READ+WRITE readiness notification therefore expects up to two processor completions, matching Tomcat's `processKey()` behavior.

### 3. Attach the token at processor creation, not after submission

`NativeEndpoint.createSocketProcessor()` is the only NativeTomcat-controlled creation point for its `SocketProcessor`. The current event-dispatch context can supply the epoch token when `processSocket()` creates/resets the processor.

If `processSocket()` returns `false`, the corresponding expected completion must be cancelled immediately. A successfully submitted processor owns exactly one completion claim.

### 4. Observe the completion of the final `Runnable.run()` envelope

`SocketProcessorBase.run()` is final and can return before `doRun()`. Therefore the completion callback must surround the complete `Runnable.run()` invocation rather than live only inside `doRun()`.

The least invasive candidate is an executor envelope used only by NativeEndpoint: it delegates to Tomcat's real endpoint executor and, for NativeTomcat's processor instances, performs:

`processor.run()` → `epoch.completeProcessor()`

inside a `finally` block.

This captures both paths:

- normal `run()` → `doRun()` → return;
- closed-before-`doRun()` → immediate return from `SocketProcessorBase.run()`.

The inherited `processSocket()` remains the submission API and Tomcat's executor remains the actual worker owner.

### 5. Finalization rule

When the epoch has no outstanding processor completion and is not cancelled/closed:

1. snapshot the wrapper's logical desired READ/WRITE interest;
2. mark the epoch finalized;
3. enqueue exactly one native final-interest command containing the epoch sequence and desired mask.

If the wrapper is closed, enqueue CLOSE instead and never REARM.

### 6. Prevent older epochs from overwriting newer state

Blocking I/O is a deliberate exception in the existing design: a blocking waiter may request interest immediately rather than wait for an ordinary processor epoch. Consequently ordinary epochs and immediate blocking-interest updates can overlap in time.

The native owner therefore needs a monotonically ordered per-connection command/epoch sequence. A final REARM from an older epoch must not overwrite a newer applied desired state. The native owner should reject stale finalization commands rather than relying on queue coalescing alone.

This is a separate invariant from command coalescing:

`queue coalescing != stale-command suppression != exactly-one effective final application`.

### 7. Close semantics

Close must atomically make the connection's epoch state terminal. A processor that later reaches the executor envelope must observe that the epoch was cancelled and perform no REARM. This also handles the case where `SocketProcessorBase.run()` starts after another path has closed the wrapper.

## Why this is preferable to the rejected alternatives

- **`doRun().finally` only:** insufficient because `SocketProcessorBase.run()` may return before `doRun()`.
- **Wait for Executor submission/dispatcher return:** insufficient because submission is not processing completion.
- **Combine READ+WRITE into one processor:** changes the pinned Tomcat dispatch shape, which currently permits separate READ and WRITE processors.
- **Direct native rearm from each processor:** preserves current race/overlap and cannot prove exactly-one final application.
- **Copy `NioEndpoint.Poller` semantics:** inappropriate because NativeTomcat uses a native `EPOLLONESHOT` owner and must bridge into Java executor completion.

## NGINX cross-check

NGINX's official event implementation maintains independent read/write event state and dispatches EPOLLIN and EPOLLOUT independently. This supports retaining independent NativeTomcat READ/WRITE desired state. NGINX does not provide an equivalent Java `SocketProcessorBase` completion boundary, so it cannot validate the executor-envelope part of this design.

## Implementation gate

Before modifying `NativeEndpoint` or `NativeSocketWrapper`, source-verify the exact Tomcat executor lifecycle methods used by `AbstractEndpoint.getExecutor()`, `createExecutor()` and `shutdownExecutor()`, then implement the smallest executor-envelope prototype.

The prototype must add tests for:

1. READ-only epoch;
2. WRITE-only epoch;
3. READ+WRITE two-processor epoch;
4. processor submission failure;
5. closed-before-`doRun()`;
6. CLOSE while processors are outstanding;
7. blocking immediate interest update overlapping an ordinary epoch;
8. stale finalization command suppression;
9. exactly one effective final REARM or CLOSE per epoch.

No runtime-equivalence or exactly-one claim is made until these tests pass.
