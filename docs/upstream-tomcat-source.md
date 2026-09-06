# Pinned Apache Tomcat source

## Source identity

NativeTomcat pins Apache Tomcat **11.0.25** as the upstream Java/container baseline.

- Upstream repository: `https://github.com/apache/tomcat.git`
- Git tag: `11.0.25`
- Tag commit: `cbe6e15ee81e2fc6232954292a80cca5d1e84009`
- Repository path: `upstream/tomcat`

The tag was independently verified against Apache's mirrored Git repository. The 11.0.25 tag points to commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`.

## Why a submodule is used at this stage

The upstream source is currently represented as an exact Git submodule rather than copied file-by-file into the NativeTomcat repository. This preserves the complete upstream tree and its original Git object identity without inventing or selectively rewriting thousands of Tomcat source files.

This is a **source pin**, not a claim that the source files are physically duplicated in the NativeTomcat Git object database. A checkout intended for compilation must initialize the submodule, for example with a recursive clone.

The submodule is intentionally pinned to a release tag commit rather than `main` or `11.0.x`, so later upstream changes cannot silently alter the baseline.

## Build consequence

Tomcat's original `build.xml` remains the formal Ant build definition inside the pinned upstream tree. NativeTomcat must not replace it with Maven, Gradle, CMake or another build system.

Before NativeTomcat claims a successful build, the build host must verify that the submodule is initialized at the recorded commit and then execute the required Ant targets from that source tree.

## Java modification policy

The upstream Java implementation is treated as the semantic baseline. NativeTomcat should add only narrowly scoped Java adapter/bootstrap code needed for the native process and native transport boundary. It must not create a parallel implementation of Catalina, Coyote, Servlet lifecycle, async state or application semantics merely to avoid using upstream code.

The existing `java/org/apache/tomcat/nativebootstrap/NativeTomcatBootstrap.java` is only a placeholder. It is not a substitute for the upstream Tomcat source and must not be treated as Tomcat integration.

## Verification gate

The following are still required before the first embedded-Tomcat milestone is considered complete:

1. checkout of the pinned submodule commit;
2. verification of the upstream `build.xml` and Java source tree;
3. ordinary Tomcat Ant compilation on a build host;
4. compilation of the NativeTomcat Java bootstrap/adapter;
5. native C/JNI compilation;
6. embedded JVM startup;
7. loading the actual Tomcat classes from the pinned source/build artifacts;
8. Catalina initialization and shutdown;
9. only then, transport integration.

No successful result for these gates is claimed merely because the source pin exists.
