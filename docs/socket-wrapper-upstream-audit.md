# SocketWrapperBase Upstream Audit — Tomcat 11.0.25

Reference commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`

Reference file:
`https://github.com/apache/tomcat/blob/cbe6e15ee81e2fc6232954292a80cca5d1e84009/java/org/apache/tomcat/util/net/SocketWrapperBase.java`

Pinned upstream blob SHA: `98785b8974054dcf89ca39ef9bcdb2d787b3819d`

Repository blob SHA: `eba0ae1c99703b15b1e9975a03d48f8268a9cd7b`

## Result

The repository `java/org/apache/tomcat/util/net/SocketWrapperBase.java` has now been fetched as its complete repository blob and compared against the complete pinned upstream blob. The repository copy is **format-normalized pinned source**: the source text is semantically identical to the pinned Tomcat 11.0.25 source, with only formatting changes introduced by the manual migration (notably tabs and brace placement) and the corresponding license URL indentation.

The raw Git blob SHA therefore correctly differs from the pinned upstream SHA. This is expected and must not be reported as an exact blob-SHA match. The relevant result is that the complete implementation, fields, methods, nested types, abstract contract and method bodies were checked against the pinned source and no semantic/source-content discrepancy was identified.

This closes the previous `SocketWrapperBase` source-migration block. It is now appropriate to proceed to the next gate: recursively verify that every supporting class required by the real wrapper is present at its original package/path, source-verified, and included in the formal build/verification paths.

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

## Build-path gate

`build.xml` and `build_test.xml` have been updated so the migrated utility/buffer/JULI/net sources are explicit members of the formal Java compile and verification paths. Both build files also contain an explicit bnd dependency gate.

The bnd dependency is therefore a **build-environment prerequisite**, not a reason to alter the pinned Tomcat source.

The latest full compilation after these source/build changes has **not** been executed in this environment. Previous successful compile/test runs predate the latest source/build changes and must not be reused as current verification.

## Tomcat integration cross-check

The pinned Tomcat transport path still requires the wrapper to participate in the real endpoint dispatch contract:

```text
NioEndpoint.Poller
    -> processKey()
    -> AbstractEndpoint.processSocket(..., dispatch=true)
    -> Executor.execute(SocketProcessor)
    -> SocketProcessorBase.run()
    -> endpoint-specific doRun()
    -> ProtocolHandler / processor
```

The pinned `NioEndpoint` source confirms that `Poller.processKey()` maps readiness to `processSocket()` and that the endpoint dispatches work to the executor. Therefore the newly verified real `SocketWrapperBase` is a prerequisite for the native transport mapping; `NativeSocketWrapper` must not be adapted against the former shell semantics.

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

## Consequence for the next gates

The previous blocker is closed. Do **not** jump directly to `Http11Processor` or Servlet execution.

The next gate is:

1. verify the complete supporting-source closure required by the real wrapper;
2. verify formal build/test membership for every migrated source;
3. satisfy the real bnd dependency prerequisite;
4. run the current build/verification suite and record the actual result;
5. only after those gates pass, begin replacing `NativeSocketWrapper` deferred methods one by one against the real `SocketWrapperBase` contract;
6. then re-check `NioSocketWrapper` and `NioEndpoint.Poller/processKey` before connecting the real `SocketProcessorBase` path.

Every Java source added or changed must continue to obey `docs/tomcat-source-migration-rule.md` and `AGENTS.md`.

## Current verification status

- pinned `SocketWrapperBase` source: **source-verified**;
- repository `SocketWrapperBase`: **format-normalized pinned source; semantic/source-content audit passed**;
- supporting sources listed above: **source-verified**;
- formal build/test paths for migrated sources: **implemented**;
- bnd dependency: **declared as a real prerequisite; environment must provide it**;
- latest full compilation after these changes: **not executed**;
- latest unit/integration suite after these changes: **not executed**;
- native transport integration: **deferred until build gate and real `NativeSocketWrapper` adaptation**;
- Servlet/TCK: **not reached**;
- benchmark: **not reached**.
