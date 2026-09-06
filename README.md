# NativeTomcat

NativeTomcat is an experimental Servlet 6.1-compatible container architecture that keeps Servlet/application semantics in Java while moving selected network/runtime work to a native C event-driven layer.

## Status

The repository is bootstrapped from an empty repository. The current implementation is an initial native runtime scaffold plus a minimal Java bootstrap hook; Servlet 6.1 compatibility and performance claims are not yet established.

## Version baseline

- Apache Tomcat: 11.0.25
- Jakarta Servlet: 6.1
- NGINX reference: 1.30.4 stable
- OpenJDK/HotSpot: JDK 17+ compatibility floor; the exact JDK remains a test variable

JDK 25 is not a project-wide requirement. Tomcat 11.0.25's general runtime/source baseline is Java 17+, while its release-build path and selected FFM/Panama OpenSSL implementation have additional JDK 22+ requirements. See `docs/baseline.md` and `docs/jdk-21-compatibility.md`.

## Build

Ant is the intended top-level build/test orchestrator. The build is not considered validated until the required JDK, Ant, C compiler/linker, JNI headers, TLS libraries and test dependencies have been checked on the target host.

## Design rule

Native code is introduced only where profiling and source-level analysis show a credible benefit that does not change Servlet-visible semantics or introduce disproportionate cross-language, synchronization, ownership or maintenance costs.

## Process entry point

The target NativeTomcat architecture uses a native C `main()` as the operating-system process entry point. The C process is responsible for creating the embedded JVM through the JNI Invocation API and coordinating native/JVM lifecycle. The current repository has not yet implemented that entry point; the existing Java bootstrap hook is only a placeholder for the later embedded-JVM integration.
