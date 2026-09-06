# SocketWrapperBase Upstream Audit — Tomcat 11.0.25

Reference commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`

Reference file:
`https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketWrapperBase.java`

## Result

The current `java/org/apache/tomcat/util/net/SocketWrapperBase.java` remains a compatibility shell. It must **not** be described as the upstream implementation.

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

## Consequence for the next gates

Do **not** start implementing more `NativeSocketWrapper` deferred methods against the current shell as if its behavior were Tomcat-compatible. First migrate the actual upstream `SocketWrapperBase` and the supporting classes required by its method bodies, then adapt the native subclass to that real contract.

The required order remains:

1. compatibility shell
2. method-by-method upstream comparison
3. migrate supporting classes required by the real bodies
4. replace `NativeSocketWrapper` deferred methods one by one
5. re-check `NioSocketWrapper`
6. re-check `NioEndpoint.Poller` / `processKey`
7. connect `SocketProcessorBase`
8. connect `Http11Processor`

Each step is subject to the mandatory Java source migration rule in `AGENTS.md`.
