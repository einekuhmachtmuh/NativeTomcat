# NativeTomcat

NativeTomcat is an experimental Servlet 6.1-compatible container architecture that keeps Servlet/application semantics in Java while moving selected network/runtime work to a native C event-driven layer.

## Status

The repository contains an initial native runtime scaffold, a native C process entry point, an embedded-JVM lifecycle bridge, and a minimal Java-side Tomcat bootstrap. The native event loop and connection primitives are implemented, but the native transport is not yet integrated with Tomcat's `SocketWrapperBase`/HTTP processing path. Servlet 6.1 compatibility and performance claims are therefore not yet established.

## Version baseline

- Apache Tomcat: 11.0.25
- Jakarta Servlet: 6.1
- NGINX reference: 1.30.4 stable
- OpenJDK/HotSpot: JDK 17+ compatibility floor; the exact JDK remains a test variable

JDK 25 is not a project-wide requirement. Tomcat 11.0.25's general runtime/source baseline is Java 17+, while its release-build path and selected FFM/Panama OpenSSL implementation have additional JDK 22+ requirements. See `docs/baseline.md` and `docs/jdk-21-compatibility.md`.

## Build

Ant is the intended top-level build/test orchestrator. The top-level Java compile target builds the pinned Tomcat source first and compiles NativeTomcat Java against the resulting Tomcat classes. The Tomcat submodule is expected to be initialized and is verified against the pinned commit before the upstream Ant build is invoked.

The build is not considered validated until the required JDK, Ant, C compiler/linker, JNI headers, TLS libraries and test dependencies have been checked on the target host.

## Design rule

Native code is introduced only where profiling and source-level analysis show a credible benefit that does not change Servlet-visible semantics or introduce disproportionate cross-language, synchronization, ownership or maintenance costs.

## Process entry point

The NativeTomcat architecture uses a native C `main()` as the operating-system process entry point. The C process dynamically loads the JVM through the JNI Invocation API, creates the embedded JVM, invokes `org.apache.tomcat.nativebootstrap.NativeTomcatBootstrap`, and coordinates JVM shutdown. The Java bootstrap sets `catalina.home`/`catalina.base` from the NativeTomcat environment and initializes and starts the pinned Tomcat Catalina lifecycle.

This embedded-JVM/bootstrap path is implemented. The remaining integration gap is the native transport-to-Tomcat connection path; the current native event loop does not yet replace Tomcat's socket transport or establish Servlet-visible compatibility.
