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

This document defines the lifecycle contract only. It does not claim that the implementation is complete.

## 2. Verified upstream Tomcat lifecycle

Tomcat 11.0.25 contains the normal Java bootstrap path under `org.apache.catalina.startup`.

`Catalina.load()` initializes naming, parses `server.xml`, obtains the `Server`, assigns Catalina/base/home state, redirects streams and invokes `Server.init()`.

`Catalina.start()` loads the server when necessary and then starts the configured server lifecycle.

The NativeTomcat bootstrap must preserve these Tomcat lifecycle semantics rather than reimplementing Server, Service, Engine, Host, Context or Servlet lifecycle in C.

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

The C entry point will use the JNI Invocation API to create the JVM. The initial implementation must explicitly construct and verify the JVM initialization arguments instead of assuming a particular installation layout.

The implementation must record:

- JVM library discovery mechanism;
- JVM option construction;
- class path/module path inputs;
- Java version observed at runtime;
- result of `JNI_CreateJavaVM()`;
- Java bootstrap class and method invoked after JVM creation.

No JDK 25 requirement is introduced by this contract. The project compatibility floor remains JDK 17+ unless a concrete feature establishes a higher minimum.

## 5. Java bootstrap boundary

The repository currently contains a minimal `NativeTomcatBootstrap` placeholder. It is not yet responsible for starting Tomcat.

Before implementing that method, the project must verify the exact Tomcat 11.0.25 source/build layout that will be present in the repository and determine whether the existing Catalina bootstrap path can be invoked from the embedded JVM without changing observable Tomcat behavior.

The preferred design is to reuse Tomcat's existing lifecycle machinery rather than create a second implementation of it.

## 6. Threading and JNI rules

A native thread that calls Java through JNI must have an appropriate `JNIEnv*`. A `JNIEnv*` is thread-local and must not be shared between threads.

Persistent Java objects referenced by native code must use JNI global references. Local references must not escape the native call in which they were created.

Native code must check for pending Java exceptions after JNI calls that can throw and must map failure into the lifecycle protocol rather than continuing with an invalid Java state.

## 7. Shutdown ordering

The shutdown protocol must prevent use-after-free across the native and Java runtimes.

The intended ordering is:

```text
shutdown requested
  -> stop accepting new native connections
  -> stop native event dispatch
  -> close/drain native transport resources according to connection policy
  -> request/complete Tomcat Java shutdown
  -> release persistent JNI references
  -> destroy JVM
  -> release remaining C process resources
  -> return from C main()
```

The exact ordering between native transport shutdown and Tomcat connector shutdown remains an implementation question that must be resolved against the actual integration point. It must not be guessed.

`nt_runtime_destroy()` must not execute concurrently with `nt_runtime_run()` under the current native runtime contract.

## 8. What is explicitly not implemented yet

At this stage the following are not claimed as working:

- a native C `main()`;
- `JNI_CreateJavaVM()` integration;
- embedded Tomcat startup;
- replacement of the standard Tomcat Connector;
- native `SocketWrapperBase` implementation;
- JNI request/response bridge;
- Servlet dispatch through the native runtime;
- graceful end-to-end shutdown of C plus JVM plus Tomcat;
- full Ant build or integration-test success in the current coding environment.

## 9. Required validation before transport integration

Before the native transport is connected to Tomcat, the project must demonstrate, on a real build host:

1. C `main()` can create the intended JVM.
2. The selected JDK and JVM library are actually usable.
3. The Java bootstrap class is loadable from the repository's built Java artifacts.
4. Tomcat 11.0.25 Catalina can initialize through the embedded path.
5. Tomcat can reach its normal initialized lifecycle state.
6. Shutdown can complete without leaked JVM/native ownership or thread races.
7. The complete path is orchestrated by the repository's Ant build/test system.

Only after these checks pass should the project implement the C/Java socket transport insertion point.

## 10. Evidence categories

Statements about Tomcat methods and lifecycle are upstream source facts and must be checked against the pinned Tomcat 11.0.25 source.

Statements about C ownership, JNI boundaries and lifecycle ordering in this document are NativeTomcat design decisions unless explicitly identified as upstream facts.

Performance, compatibility and race-freedom claims require executed tests or benchmarks; they are not established by this design document.
