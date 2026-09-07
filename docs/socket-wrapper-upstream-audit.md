# SocketWrapperBase Upstream Audit — Tomcat 11.0.25

Reference commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`

Reference file:
`https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketWrapperBase.java`

Pinned upstream blob SHA: `98785b8974054dcf89ca39ef9bcdb2d787b3819d`

Repository blob SHA: `eba0ae1c99703b15b1e9975a03d48f8268a9cd7b`

## Result

The repository `java/org/apache/tomcat/util/net/SocketWrapperBase.java` has now been fetched as its complete repository blob and compared against the complete pinned upstream blob. The repository copy is **format-normalized pinned source**: the source text is semantically identical to the pinned Tomcat 11.0.25 source, with only formatting changes introduced by the manual migration (notably tabs and brace placement) and the corresponding license URL indentation.

The raw Git blob SHA therefore correctly differs from the pinned upstream SHA. This is expected and must not be reported as an exact blob-SHA match. The relevant result is that the complete implementation, fields, methods, nested types, abstract contract and method bodies were checked against the pinned source and no semantic/source-content discrepancy was identified.

This closes the previous `SocketWrapperBase` source-migration block.

## Verified upstream structure

The audit covered the complete class rather than only the public surface, including:

- imports and superclass/generic contract;
- connection ID generation and wrapper state;
- endpoint reference and `ReentrantLock` ownership;
- close/error/previous-I/O-error state;
- read/write timeout state and keep-alive state;
- local/remote address, host and port caches;
- `ServletConnection`, SNI and negotiated-protocol state;
- `SocketBufferHandler` and `WriteBuffer` integration;
- asynchronous-operation semaphores and `OperationState`;
- current-processor association;
- executor dispatch and endpoint-running checks;
- read-buffer population and `unRead()` semantics;
- close lifecycle, handler release, connection counting and `doClose()`;
- blocking and non-blocking byte-array/ByteBuffer writes;
- blocking/non-blocking flush semantics;
- `processSocket()` delegation;
- read/write interest registration;
- sendfile and SSL abstract contracts;
- NIO2 blocking modes and completion states;
- vectored I/O operation state and completion handling;
- timeout, pending-operation and completion-handler behavior;
- transfer/buffer utility methods;
- `ServletConnection` creation.

No shell-only replacement remains in this class.

## Supporting-source closure

The following exact pinned Tomcat sources required by this wrapper are already present and source-verified at their original paths:

| Source | Pinned blob SHA | Repository status |
|---|---|---|
| `org/apache/tomcat/util/net/SocketBufferHandler.java` | `fc6888b5326d9b0301b2e4d692eff1ea199d790f` | exact source + SHA verified |
| `org/apache/tomcat/util/buf/ByteBufferUtils.java` | `196bc9b6b79477808f26279ea79447996db66707` | exact source + SHA verified |
| `org/apache/tomcat/util/buf/ByteBufferUtilsUnsafe.java` | `1ce3db09321e06d08c534c37e54bd16e99fcc723` | exact source + SHA verified |
| `org/apache/tomcat/util/ExceptionUtils.java` | `485c66c658b18653021f12053b37ab78f4358b8c` | exact source + SHA verified |
| `org/apache/tomcat/util/res/StringManager.java` | `a904c586600ff54b47ac1550d36e2d23d0732b87` | exact source + SHA verified |
| `org/apache/juli/logging/Log.java` | `11de9d593708410f4e43beb40a656ed293e54b97` | exact source + SHA verified |
| `org/apache/juli/logging/LogFactory.java` | `1696c84ef800d11d5b3d81c20ddf17e5e0d94b6b` | exact source + SHA verified |
| `org/apache/juli/logging/DirectJDKLog.java` | `70226d1b3c463b019f1a6d556df166bfeb7a3405` | exact source + SHA verified |
| `org/apache/juli/logging/LogConfigurationException.java` | `9e296ed1ce4b1c78eb2839d90ae7b083711ca1f9` | exact source + SHA verified |

`LogFactory.java` also has a real third-party bnd annotation dependency (`aQute.bnd.annotation.spi.ServiceConsumer`). This must be supplied as a real build dependency; it must not be replaced by a locally invented annotation or removed merely to make the source compile.

## NativeSocketWrapper audit — next gate

With the real `SocketWrapperBase` now verified, the current `NativeSocketWrapper.java` was audited against the pinned Tomcat contract and the pinned `NioEndpoint.NioSocketWrapper` implementation. It is still only a **surface proof**, not a transport implementation.

Verified mismatches/deferred contracts:

| Area | Current NativeSocketWrapper | Pinned Tomcat/NIO requirement |
|---|---|---|
| native read | throws `UnsupportedOperationException` | perform buffered/native socket read with blocking/non-blocking semantics |
| native ByteBuffer read | throws `UnsupportedOperationException` | preserve `SocketBufferHandler` semantics and direct-read optimization where applicable |
| `isReadyForRead()` | returns `!isClosed()` | inspect internal read buffer and attempt non-blocking fill; readiness is not merely lifecycle state |
| address metadata | placeholder strings / `-1` ports | expose real transport metadata or an explicitly verified native equivalent |
| `doClose()` | does not release native handle | exactly-once close must release transport ownership and clear/reset wrapper state safely |
| native write | throws `UnsupportedOperationException` | implement blocking/non-blocking write against wrapper buffers and native transport |
| `flushNonBlocking()` | returns `hasDataToWrite()` without flushing | actually drain socket/network/non-blocking buffers and report remaining data |
| read interest | boolean flag only | interest registration must reach the native event-loop owner and correspond to actual readiness/consumption |
| write interest | boolean flag only | same ownership requirement; pending writes must cause native write interest |
| sendfile | unsupported | deferred until native transport contract is defined and verified |
| TLS/client auth | unsupported | deferred; no fake TLS surface should be added |
| vectored async I/O | unsupported | deferred until required native async/vectored semantics are explicitly designed and verified |

The most important immediate finding is that `isReadyForRead()` cannot remain `!isClosed()`: the pinned NIO implementation checks buffered data and performs a non-blocking fill before reporting readiness. The native implementation therefore needs a real transport-read path before this method can be made semantically meaningful.

The pinned NIO implementation also shows that close is more than `fd close`: it removes the connection from the endpoint registry, closes/resets/recycles the channel, clears buffers, resets the wrapper socket and closes any pending sendfile channel. NativeTomcat will require an equivalent ownership/lifetime design adapted to the native connection object rather than copying NIO-specific channel recycling.

## Tomcat integration cross-check

The pinned Tomcat transport path requires the wrapper to participate in the real endpoint dispatch contract:

```text
NioEndpoint.Poller
    -> processKey()
    -> AbstractEndpoint.processSocket(..., dispatch=true)
    -> Executor.execute(SocketProcessor)
    -> SocketProcessorBase.run()
    -> endpoint-specific doRun()
    -> ProtocolHandler / processor
```

The pinned `NioEndpoint` source confirms that a ready key is first unregistered from current readiness interest, then read/write work is dispatched through `processSocket()`; close occurs if dispatch fails. The wrapper therefore cannot fake interest with local booleans. fileciteturn309file0L2-L2

The pinned NIO wrapper also uses its endpoint/poller as the owner of interest registration and maintains per-wrapper read/write state, timeout timestamps, buffers and close/recycle behavior. fileciteturn310file0L2-L2

## NGINX cross-check

NGINX remains an architectural cross-check only. Its event subsystem explicitly separates event polling, read/write event handling and event registration/interest management. The current NGINX event sources continue to support the project rule that kernel readiness, native event handling, transport consumption and higher-level processing are separate stages. citeturn0view0turn0view1

This does not change the Tomcat baseline and does not justify changing `SocketWrapperBase` semantics. In particular, an `EPOLLONESHOT` readiness notification cannot be treated as equivalent to completed Java/Tomcat transport consumption.

The intended integration remains:

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

## Build-path gate

`build.xml` and `build_test.xml` have been updated so the migrated utility/buffer/JULI/net sources are explicit members of the formal Java compile and verification paths. Both build files also contain an explicit bnd dependency gate.

The bnd dependency is therefore a **build-environment prerequisite**, not a reason to alter the pinned Tomcat source. The current formal build declares `BND_JAR` as the environment-provided location for the real bnd JAR. fileciteturn306file0L2-L2

The latest full compilation after these source/build changes has **not** been executed in this environment. Previous successful compile/test runs predate the latest source/build changes and must not be reused as current verification.

## Consequence for the next gates

The previous blocker is closed. The next implementation gate is **not** HTTP or Servlet code. It is the native transport contract needed by `NativeSocketWrapper`:

1. establish stable native-handle ↔ Java wrapper ownership and lookup;
2. add the minimum native read/write/close bridge needed by the wrapper;
3. implement real wrapper buffering/readiness/flush semantics against those primitives;
4. make read/write-interest requests return to the native event-loop owner rather than mutate local flags only;
5. verify `NioEndpoint`/`AbstractEndpoint.processSocket`/`SocketProcessorBase` dispatch semantics again before connecting them;
6. only then proceed toward `ProtocolHandler` / `Http11Processor`.

Do not add sendfile/TLS/vectored I/O merely to eliminate `UnsupportedOperationException`; those remain separate deferred gates until their contracts are required.

Every Java source added or changed must continue to obey `docs/tomcat-source-migration-rule.md` and `AGENTS.md`.

## Current verification status

- pinned `SocketWrapperBase` source: **source-verified**;
- repository `SocketWrapperBase`: **format-normalized pinned source; semantic/source-content audit passed**;
- supporting sources listed above: **source-verified**;
- `NativeSocketWrapper`: **surface-only; deferred methods audited against pinned NIO contract**;
- formal build/test paths for migrated sources: **implemented**;
- bnd dependency: **declared as a real prerequisite; environment must provide it**;
- latest full compilation after these changes: **not executed**;
- latest unit/integration suite after these changes: **not executed**;
- native transport integration: **deferred**;
- Servlet/TCK: **not reached**;
- benchmark: **not reached**.
