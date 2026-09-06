# Native entry point and JVM lifecycle

## 1. Scope and status

The target NativeTomcat process entry point is native C `main()`. This document defines process/JVM ownership and the current bootstrap boundary. It does not claim that embedded Tomcat startup or native-transport HTTP processing has been runtime-verified.

Evidence levels used below:

- **implemented** — repository code contains the path;
- **source-verified** — the relevant Tomcat implementation was checked against pinned 11.0.25 source;
- **compiled/tested** — an executable build/test path passed;
- **runtime-verified** — the integrated behavior actually executed on a suitable host;
- **deferred/blocked** — intentionally postponed or currently not executable.

## 2. Target lifecycle

```text
OS process
  -> native C main()
  -> JVM Invocation API
  -> embedded HotSpot JVM
  -> Tomcat Java bootstrap/lifecycle
  -> native transport runtime
  -> coordinated shutdown
  -> JVM destruction
  -> C process exit
```

The current implementation reaches the JVM and Java bootstrap boundaries. The native transport is not yet integrated into Tomcat's real connector processing path.

## 3. Tomcat lifecycle authority

Tomcat 11.0.25's Java bootstrap/Catalina lifecycle remains the authority for Server, Service, Engine, Host, Context and application lifecycle semantics.

NativeTomcat must not reimplement these lifecycles in C merely to obtain a native process entry point.

## 4. Native process ownership

C owns:

- OS process entry and exit status;
- JVM creation/destruction through the Invocation API;
- NativeTomcat native runtime initialization/shutdown;
- native event-loop and native connection resources.

Tomcat/Java owns:

- Catalina and Server lifecycle semantics;
- Connector/Protocol/Coyote lifecycle unless a later verified transport integration delegates selected operations to native code;
- HTTP protocol processing;
- Catalina/Servlet application lifecycle;
- Servlet, Filter, Listener and Async semantics.

A native worker/event-loop thread may call Java through JNI only under the JVM thread-attachment rules.

## 5. Current JVM creation path

The native entry point dynamically loads the JVM library under `JAVA_HOME/lib/server/libjvm.so`, constructs the configured JVM options/class path, and invokes `JNI_CreateJavaVM()`.

The bootstrap class is:

`org.apache.tomcat.nativebootstrap.NativeTomcatBootstrap`

The current configuration uses `NATIVETOMCAT_JAVA_CP` for the Java class path and the bootstrap requires `NATIVETOMCAT_CATALINA_HOME`, with optional `NATIVETOMCAT_CATALINA_BASE`.

These are implementation facts. Successful embedded Tomcat execution still requires a complete pinned Tomcat runtime/class path on a suitable host.

## 6. Java bootstrap boundary

`NativeTomcatBootstrap` delegates to Tomcat's `org.apache.catalina.startup.Bootstrap` and invokes the normal `init()` / `start()` path. Shutdown invokes the corresponding stop path.

This is a reuse of Tomcat lifecycle code, not a parallel Catalina implementation.

The source-level delegation has been checked against the pinned Tomcat baseline. That source verification does not by itself prove that the complete embedded runtime starts successfully in the current local environment.

## 7. JNI thread rules

`JNIEnv*` is thread-specific. Native code must establish a valid environment for the calling thread before making JNI calls and must detach a thread that it attached before that thread terminates.

Persistent Java references retained by native code must be JNI global references. Local references must not escape their JNI call scope.

Every JNI operation that can raise a Java exception must be checked. A pending exception is a failure state unless the exact API contract explicitly handles it.

The current JVM bridge retains the bootstrap class as a global reference and attaches native event-loop threads when they dispatch Java events.

## 8. Shutdown ownership

The current JVM smoke lifecycle is:

```text
C main
  -> create JVM
  -> Java Bootstrap.init/start
  -> wait for native stop request
  -> Java Bootstrap.stop
  -> release persistent bootstrap reference
  -> DestroyJavaVM
  -> C exit
```

Native transport integration adds another dependency:

```text
stop accepting
  -> stop native event dispatch
  -> drain/terminate Java transport work
  -> release wrapper/native references
  -> destroy native connections
  -> stop Tomcat
  -> destroy JVM
```

The latter is a **required design direction, not an implemented final protocol**. Its exact ordering must be derived from the actual Java wrapper lifetime and Tomcat connector shutdown path before being frozen.

The current native runtime also requires `nt_runtime_run()` to return before `nt_runtime_destroy()`.

## 9. Current implementation status

### Implemented / source-level

- native C `main()`;
- JVM Invocation API loading/creation;
- Java bootstrap class lookup/invocation;
- delegation to Tomcat `Bootstrap.init()` / `start()` / `stop()`;
- bootstrap global-reference management;
- native wait/stop coordination;
- Ant gates for pinned-source/build verification.

### Compiled/tested

The repository's current NativeTomcat Java surface and native smoke tests have executable verification paths. Those tests do not constitute embedded Tomcat HTTP integration.

### Not runtime-verified in the current environment

- complete pinned Tomcat Ant build;
- complete embedded Tomcat startup;
- connector bind/listen through the embedded lifecycle;
- HTTP request/response through the NativeTomcat transport;
- race-free shutdown with queued native-to-Java transport work;
- replacement of the normal Tomcat Connector.

## 10. Preconditions for transport integration

Before treating native transport as part of the running Tomcat connector, the project must verify on a suitable build host:

1. exact pinned Tomcat source is available;
2. the required Tomcat runtime is built;
3. the embedded bootstrap starts the configured Tomcat lifecycle;
4. the configured connector reaches its normal running state;
5. shutdown completes cleanly;
6. native connection ownership and Java wrapper ownership have an explicit lifetime protocol;
7. the native event path reaches the real Tomcat `processSocket()` / `SocketProcessor` path;
8. an HTTP smoke test passes through that real path.

Only then should end-to-end Servlet behavior be evaluated.

## 11. Non-claims

This document does not establish:

- successful embedded Tomcat runtime execution merely from source-level delegation;
- native transport replacement of the Tomcat Connector;
- Servlet/TCK compatibility;
- leak/race freedom under the final transport path;
- benchmark performance;
- a final shutdown order for integrated native transport.
