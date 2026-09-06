# Native entry point and JVM lifecycle

## 1. Architectural requirement

The target NativeTomcat process entry point is a native C `main()`.

The operating-system process must therefore enter NativeTomcat through C rather than through `org.apache.catalina.startup.Bootstrap.main()` as the process entry point.

The intended high-level lifecycle is:

```text
OS process
  -> native C main()
  -> JVM Invocation API
  -> embedded HotSpot JVM
  -> Tomcat Java bootstrap/lifecycle
  -> native transport runtime
  -> coordinated shutdown
  -> JVM shutdown
  -> C process exit
```

This document defines the lifecycle contract and records the current implementation boundary. It does not claim end-to-end runtime success until the required build-host tests have been executed.

## 2. Verified upstream Tomcat lifecycle

Tomcat 11.0.25 contains the normal Java bootstrap path under `org.apache.catalina.startup`.

`Catalina.load()` initializes naming, parses `server.xml`, obtains the `Server`, assigns Catalina/base/home state, redirects streams and invokes `Server.init()`.

`Catalina.start()` loads the server when necessary and then starts the configured server lifecycle.

The NativeTomcat bootstrap reuses Tomcat's existing `Bootstrap` and Catalina lifecycle machinery rather than reimplementing Server, Service, Engine, Host, Context or Servlet lifecycle in C.

## 3. Native process ownership

The native process owns:

- the OS process entry point;
- JVM creation and destruction;
- native transport runtime initialization and shutdown;
- native event-loop ownership;
- native socket/connection resources;
- final process exit status.

Java/Tomcat owns:

- Catalina and Server lifecycle semantics;
- Connector/Protocol/Coyote lifecycle semantics unless a later verified integration point delegates selected transport work to native code;
- HTTP/Servlet processing;
- application lifecycle and class loading;
- Servlet, Filter, Listener and Async semantics.

Ownership does not imply that all native work is performed by the C `main()` thread. Native event-loop and worker threads may be created after initialization subject to the JVM/JNI thread-attachment contract.

## 4. JVM creation

The C entry point uses the JNI Invocation API to create the JVM. The implementation explicitly constructs and verifies the JVM initialization arguments instead of assuming that the JVM is already running.

The implementation records/configures:

- JVM library discovery through `JAVA_HOME/lib/server/libjvm.so`;
- JVM option construction;
- Java class path supplied through `NATIVETOMCAT_JAVA_CP`;
- Java bootstrap class `org.apache.tomcat.nativebootstrap.NativeTomcatBootstrap`;
- the result of `JNI_CreateJavaVM()` and subsequent Java bootstrap failure through the native lifecycle status.

No JDK 25 requirement is introduced by this implementation. The project compatibility floor remains JDK 17+ unless a concrete feature establishes a higher minimum.

## 5. Java bootstrap boundary

`NativeTomcatBootstrap` is now a Java-side lifecycle bridge. It requires `NATIVETOMCAT_CATALINA_HOME`, optionally accepts `NATIVETOMCAT_CATALINA_BASE`, sets the corresponding `catalina.home` and `catalina.base` system properties, then invokes Tomcat's `Bootstrap.init()` followed by `Bootstrap.start()`.

Shutdown invokes the corresponding Tomcat `Bootstrap.stop()` path.

This preserves the Tomcat bootstrap/classloader/lifecycle implementation rather than introducing a parallel NativeTomcat implementation of Catalina or Server lifecycle.

The source-level integration has been verified against the pinned Tomcat 11.0.25 source. Actual startup still requires execution on a build host with the pinned submodule, generated Tomcat runtime, compatible JDK, and a complete Java class path.

## 6. Threading and JNI rules

A native thread that calls Java through JNI must have an appropriate `JNIEnv*`. A `JNIEnv*` is thread-local and must not be shared between threads.

Persistent Java objects referenced by native code must use JNI global references. Local references must not escape the native call in which they were created.

Native code must check for pending Java exceptions after JNI calls that can throw and must map failure into the lifecycle protocol rather than continuing with an invalid Java state.

The current JVM bridge keeps the persistent bootstrap class as a JNI global reference and performs Java bootstrap/shutdown calls on the JVM-owning native thread.

## 7. Shutdown ordering

The shutdown protocol must prevent use-after-free across the native and Java runtimes.

The current JVM smoke-lifecycle ordering is:

```text
C main()
  -> create JVM
  -> Java Bootstrap.init/start
  -> wait for native stop request
  -> Java Bootstrap.stop
  -> release bootstrap global reference
  -> DestroyJavaVM
  -> C process exit
```

The final ordering for integrated native transport remains an implementation question. Once native transport is connected, shutdown must additionally coordinate event dispatch, accepting sockets, connection ownership and Tomcat Connector shutdown. It must not be guessed in advance.

`nt_runtime_destroy()` must not execute concurrently with `nt_runtime_run()` under the current native runtime contract.

## 8. Current implementation status

Implemented at source level:

- native C `main()`;
- JNI Invocation API JVM creation;
- Java bootstrap class lookup and invocation;
- Java-side delegation to Tomcat `Bootstrap.init()` / `start()` / `stop()`;
- JNI global reference management for the bootstrap class;
- native wait/stop coordination around the embedded JVM;
- Ant gates for verifying the pinned Tomcat commit, compiling Tomcat, building a working Tomcat runtime, and compiling NativeTomcat Java against the pinned Tomcat classes.

Not yet claimed as runtime-verified:

- successful execution of the complete Ant build on a real build host;
- successful embedded Tomcat startup;
- successful connector bind/listen;
- HTTP request/response through the embedded server;
- leak/race-free end-to-end shutdown under the real runtime;
- replacement of the standard Tomcat Connector;
- native `SocketWrapperBase` implementation;
- JNI request/response transport bridge;
- Servlet dispatch through the native runtime.

## 9. Required validation before transport integration

Before the native transport is connected to Tomcat, the project must demonstrate, on a real build host:

1. C `main()` can create the intended JVM.
2. The selected JDK and JVM library are actually usable.
3. The Java bootstrap class is loadable from the repository's built Java artifacts.
4. Tomcat 11.0.25 Catalina can initialize through the embedded path.
5. Tomcat can reach its normal initialized/running lifecycle state.
6. Shutdown can complete without leaked JVM/native ownership or thread races.
7. The complete path is orchestrated by the repository's Ant build/test system.
8. A local HTTP smoke test can confirm that the configured Tomcat connector actually accepts a request.

Only after these checks pass should the project implement the C/Java socket transport insertion point.

## 10. Evidence categories

Statements about Tomcat methods and lifecycle are upstream source facts and must be checked against the pinned Tomcat 11.0.25 source.

Statements about C ownership, JNI boundaries and lifecycle ordering in this document are NativeTomcat design decisions unless explicitly identified as upstream facts.

Source-level integration is not equivalent to runtime success. Performance, compatibility, absence of races and end-to-end shutdown correctness require executed tests or benchmarks; they are not established by this document.
