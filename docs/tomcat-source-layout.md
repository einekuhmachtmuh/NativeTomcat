# Tomcat 11.0.25 source layout

## 1. Upstream baseline

NativeTomcat uses Apache Tomcat 11.0.25 as its Java implementation baseline.

Pinned commit:

`cbe6e15ee81e2fc6232954292a80cca5d1e84009`

The upstream repository is declared as the `upstream/tomcat` Git submodule. The submodule declaration does not imply that the submodule is initialized in every checkout; build steps that require upstream source must verify the exact commit before using it.

## 2. Source authority

For Tomcat implementation behavior, the pinned 11.0.25 source is authoritative. NativeTomcat documentation, compatibility shells, API documentation, and previous plans are not substitutes for the source.

For application-visible Servlet semantics, Jakarta Servlet 6.1 is authoritative. NGINX is an architectural cross-check for native event design, not a Tomcat implementation authority.

## 3. Repository Java tree

NativeTomcat may contain two kinds of Java source:

1. **NativeTomcat-specific integration/bootstrap code**, such as `org.apache.tomcat.nativebootstrap.*`;
2. **exact migrated Tomcat source**, placed under `java/` at the original package path when required by the integration.

A compatibility shell is not equivalent to migrated upstream source.

The mandatory migration procedure is defined in `docs/tomcat-source-migration-rule.md` and applies recursively to supporting classes.

## 4. Current bootstrap boundary

`org.apache.tomcat.nativebootstrap.NativeTomcatBootstrap` is NativeTomcat-specific bridge code. It delegates lifecycle operations to Tomcat's existing `org.apache.catalina.startup.Bootstrap` through the verified integration path rather than reimplementing Catalina lifecycle in C.

It is therefore incorrect to describe the current bootstrap as "not starting Tomcat". Source-level delegation to `Bootstrap.init()` / `Bootstrap.start()` exists. What remains unverified is successful execution of the complete embedded Tomcat runtime on the current local host, because the pinned upstream submodule/runtime is not locally available.

## 5. Build authority

`upstream/tomcat/build.xml` remains the authoritative build for Tomcat itself.

NativeTomcat's top-level `build.xml` is the project build entry point for NativeTomcat-specific compilation, native compilation/tests, and explicit upstream compatibility gates. It must not silently replace the upstream Tomcat build.

`build_test.xml` is an intermediate verification build. A passing NativeTomcat surface test is not a successful Tomcat build or integration test.

## 6. JDK baseline

The pinned Tomcat 11.0.25 build metadata establishes a Java 17 compilation baseline, while particular release/build paths require a newer JDK. Therefore:

- JDK 17+ is the project's stated compatibility floor for code intended to remain on the Java 17 baseline;
- the local JDK 21 environment can compile the current NativeTomcat Java surface;
- this does not prove that every Tomcat 11.0.25 release-build path succeeds on JDK 21;
- any feature requiring a higher JDK must explicitly raise its gate and document why.

## 7. Source-verification rule

Whenever a Tomcat Java implementation class materially affects an implementation step:

1. locate the exact file in `java/`;
2. if absent, obtain the exact pinned source before treating the class as migrated;
3. verify supporting classes recursively;
4. update both formal and verification build paths;
5. compile/test the resulting surface;
6. distinguish source verification from runtime integration.

If exact pinned source cannot currently be obtained or materialized into the repository, the step is **source-verification incomplete/blocked**. No compatibility shell may be described as equivalent to the missing implementation.
