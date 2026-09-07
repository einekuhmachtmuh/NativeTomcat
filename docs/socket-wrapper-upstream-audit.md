# SocketWrapperBase Upstream Audit — Tomcat 11.0.25

Reference commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`

Reference file:
`https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketWrapperBase.java`

Pinned upstream blob SHA: `98785b8974054dcf89ca39ef9bcdb2d787b3819d`

## Result

The current `java/org/apache/tomcat/util/net/SocketWrapperBase.java` remains a compatibility shell. It must **not** be described as the upstream implementation. The exact pinned source was successfully retrieved and independently verified by its upstream blob SHA, but the current repository could not yet receive the complete file through the available repository write path. The attempted one-shot GitHub Actions migration was removed because repository Actions reported no workflow runs; it is not treated as a successful migration.

The pinned upstream source materially differs from the shell in state, synchronization, buffering, asynchronous I/O, executor dispatch, error handling, close/recycle behavior, Servlet connection handling, and the concrete abstract-method contract.

Verified upstream dependencies visible directly in the pinned source include:

- `AbstractEndpoint`
- `SocketBufferHandler`
- `WriteBuffer`
- `ApplicationBufferHandler`
- `SendfileDataBase`
- `SendfileState`
- `SSLSupport`
- `SocketEvent`
- `org.apache.tomcat.util.ExceptionUtils`
- `org.apache.tomcat.util.res.StringManager`
- `org.apache.juli.logging.Log` / `LogFactory`
- `jakarta.servlet.ServletConnection`

The upstream class also contains connection-ID generation, read/write timeout state, keep-alive state, cached local/remote metadata, SNI and negotiated-protocol state, error recording, non-blocking write buffering, asynchronous-operation semaphores/state, processor association, and vectored-I/O operation machinery. These are implementation dependencies, not merely API-surface decorations.

## Supporting-source migrations completed in this gate

Two small supporting Tomcat sources required directly by the upstream wrapper have now been migrated exactly at their original paths and their repository blob SHA matches the pinned upstream SHA:

| Source | Pinned blob SHA | Repository status |
|---|---|---|
| `org/apache/tomcat/util/ExceptionUtils.java` | `485c66c658b18653021f12053b37ab78f4358b8c` | exact source migrated and SHA-verified |
| `org/apache/tomcat/util/res/StringManager.java` | `a904c586600ff54b47ac1550d36e2d23d0732b87` | exact source migrated and SHA-verified |

`build.xml` was updated so these migrated source paths are explicitly included in the formal Java build slice. They are not being treated as invisible dependencies.

The remaining `org.apache.juli.logging.*` classes and the rest of the Tomcat dependency closure have not been migrated merely to make the current shell compile. They must be migrated when required by the exact upstream wrapper and recursively audited under the same source rule.

## Shell-vs-upstream findings

The following current shell behaviors are deliberately temporary and are not acceptable as final Tomcat semantics:

| Area | Current shell | Required direction |
|---|---|---|
| `hasDataToRead()` | Always returns `true` | Restore upstream buffering/read semantics |
| `hasDataToWrite()` | Only checks the simplified local write buffer | Restore socket-buffer + non-blocking buffer semantics |
| `isReadyForWrite()` | Directly delegates to simplified `canWrite()` | Restore upstream readiness/interest behavior |
| `canWrite()` | Simplified closed/buffer check | Restore upstream write-state contract |
| `flush(boolean)` | Delegates to `flushNonBlocking()` | Restore blocking/non-blocking distinction |
| `unRead()` | Throws `UnsupportedOperationException` | Migrate required push-back semantics |
| vectored I/O | Explicitly unsupported | Migrate upstream operation-state machinery before enabling it |
| executor dispatch | Simplified endpoint delegation | Restore upstream endpoint/executor contract |
| async operation state | Placeholder nested classes | Migrate the real state/handler implementation |
| error handling | Simplified first-write-wins field | Compare and restore exact upstream semantics |
| close/recycle | Simplified `AtomicBoolean` path | Compare exact upstream lifecycle and subclass hooks |

## Tomcat integration cross-check

The pinned Tomcat transport path requires the wrapper to participate in the real endpoint dispatch contract rather than being called as an isolated facade. The required path remains:

```text
NioEndpoint.Poller
    -> processKey()
    -> AbstractEndpoint.processSocket(..., dispatch=true)
    -> Executor.execute(SocketProcessor)
    -> SocketProcessorBase.run()
    -> endpoint-specific doRun()
    -> ProtocolHandler / processor
```

Therefore replacing the shell is a prerequisite for a real native-handle-to-Tomcat transport mapping. A surface-compatible wrapper cannot establish the lifecycle, locking, buffering or processor association required by this path.

## NGINX cross-check

NGINX remains an architectural cross-check only. Its event subsystem separates readiness polling, read/write event registration and handler dispatch. That supports keeping native event ownership and rearm separate from Java/Tomcat processing.

It does **not** justify implementing Tomcat wrapper semantics differently from pinned Tomcat, and it does not justify rearming `EPOLLONESHOT` when Java work has merely been queued.

The native integration therefore remains:

```text
kernel readiness
    -> native event owner
    -> stable handle ↔ Java transport object
    -> real Tomcat SocketWrapperBase
    -> processSocket()
    -> SocketProcessorBase
    -> actual transport consumption
    -> native owner decides interest/close
    -> exactly one rearm / close
```

## Consequence for the next gates

Do **not** start implementing more `NativeSocketWrapper` deferred methods against the current shell as if its behavior were Tomcat-compatible. First complete the actual upstream `SocketWrapperBase` migration and the supporting classes required by its method bodies, then adapt the native subclass to that real contract.

The required order remains:

1. exact pinned `SocketWrapperBase` migration;
2. recursively migrate its required supporting source closure;
3. formal-build and verification path for every migrated source;
4. replace `NativeSocketWrapper` deferred methods one by one;
5. re-check `NioSocketWrapper` semantics;
6. re-check `NioEndpoint.Poller` / `processKey`;
7. connect the real `SocketProcessorBase` path;
8. only then connect `ProtocolHandler` / `Http11Processor`.

Each step is subject to the mandatory Java source migration rule in `AGENTS.md` and `docs/work-principles.md`.

## Current verification status

- pinned `SocketWrapperBase` source: **source-verified**;
- exact `SocketWrapperBase` repository migration: **blocked**;
- `ExceptionUtils`: **implemented + SHA-verified**;
- `StringManager`: **implemented + SHA-verified**;
- formal build path updated for migrated utility sources: **implemented**;
- latest full compilation after these changes: **not executed**;
- CI for latest repository state: **no workflow status available**;
- native transport integration: **deferred until exact wrapper migration is complete**.
