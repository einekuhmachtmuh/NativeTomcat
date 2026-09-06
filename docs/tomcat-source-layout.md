# Tomcat 11.0.25 source layout

NativeTomcat pins Apache Tomcat 11.0.25 as the upstream Java baseline through the Git submodule at `upstream/tomcat`.

The upstream repository is pinned to commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009` (Tomcat 11.0.25). The submodule declaration is recorded in `.gitmodules`; the submodule working tree is therefore not duplicated into the NativeTomcat repository's own tree.

## Build authority

`upstream/tomcat/build.xml` is the authoritative Ant build for Tomcat. The NativeTomcat top-level `build.xml` is intentionally limited to the NativeTomcat Java bootstrap smoke build. It must not be treated as a replacement for Tomcat's build system.

## Java modification policy

Tomcat Java sources under `upstream/tomcat` remain upstream unless a specific integration point has been verified and an explicit NativeTomcat patch is justified. NativeTomcat-specific Java code belongs outside the upstream tree unless the integration requires a minimal upstream change.

The first Java class is `java/org/apache/tomcat/nativebootstrap/NativeTomcatBootstrap.java`. It is only a bootstrap seam; it is not a reimplementation of Catalina and currently does not start Tomcat.

## Verification status

The upstream 11.0.25 `build.xml` has been inspected and records Servlet 6.1 and a Java 17 compilation baseline. Its release build path additionally declares Java 22. Local JDK 21 can therefore be used for NativeTomcat's Java 17-compatible smoke compilation, but it cannot be represented as a successful Tomcat 11.0.25 release build environment.

A submodule declaration alone does not mean that the submodule has been initialized in every checkout. Any build script that depends on `upstream/tomcat` must verify that the expected pinned commit is present before invoking the upstream Ant build.
