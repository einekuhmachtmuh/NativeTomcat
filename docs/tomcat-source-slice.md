# Tomcat Java source slice for the native bridge

Pinned upstream: Apache Tomcat commit `cbe6e15ee81e2fc6232954292a80cca5d1e84009`.

The repository now starts a self-hosted Tomcat Java source tree under the same package layout used by upstream:

- `java/org/apache/catalina/Server.java`
- `java/org/apache/catalina/Service.java`
- `java/org/apache/coyote/ProtocolHandler.java`

These are the Tomcat type boundaries that the native bootstrap previously referenced directly. `NativeTomcatBootstrap`
now uses reflection for lifecycle discovery so the intermediate `build_test.xml` can compile and verify the native bridge
without requiring an external Tomcat class output directory.

This is deliberately an incremental source migration, not a claim that the complete Tomcat source tree has already been
copied. The next migration steps should add the concrete classes needed for the real native-backed `SocketWrapperBase`
and `SocketProcessorBase` path, while keeping every file at the pinned Tomcat revision until it is intentionally modified.

`build_test.xml` is the authoritative intermediate verification entry point. It verifies the source-slice presence, compiles
the native Java bridge, runs the Java dispatcher test, runs the JNI dispatch test, runs the native runtime test, and performs
a strict native compile with `-Werror`. It does not invoke `upstream/tomcat/build.xml`.
