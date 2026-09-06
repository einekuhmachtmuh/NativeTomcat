# NativeTomcat

NativeTomcat is an experimental Servlet 6.1-compatible container architecture that keeps Servlet/application semantics in Java while moving selected network/runtime work to a native C event-driven layer.

## Status

The repository is bootstrapped from an empty repository. The current implementation is an initial native runtime scaffold; Servlet 6.1 compatibility and performance claims are not yet established.

## Version baseline

- Apache Tomcat: 11.0.25
- Jakarta Servlet: 6.1
- NGINX reference: 1.30.4 stable
- OpenJDK/HotSpot: JDK 25

Exact upstream commits/tags and build-environment verification are recorded in `docs/baseline.md`.

## Build

Ant is the intended top-level build/test orchestrator. The build is not considered validated until the required JDK, Ant, C compiler/linker, JNI headers, TLS libraries and test dependencies have been checked on the target host.

## Design rule

Native code is introduced only where profiling and source-level analysis show a credible benefit that does not change Servlet-visible semantics or introduce disproportionate cross-language, synchronization, ownership or maintenance costs.
