# NativeTomcat build entry points

## `build.xml` — formal build

`build.xml` is the **formal NativeTomcat build entry point**. It is intended to become the authoritative build as the repository-owned Tomcat Java source tree is migrated and modified. It must not depend on `build_test.xml`.

The current `java-compile` target compiles the Java source tree owned by this repository. The upstream Tomcat Ant build remains available only as an explicit `tomcat-compile` / `tomcat-deploy` compatibility gate while migration is incomplete.

## `build_test.xml` — build verification

`build_test.xml` is **BUILD VERIFICATION ONLY**. It is deliberately independent of `upstream/tomcat/build.xml` and is used for intermediate, component-level verification while the NativeTomcat Java tree is being reconstructed and modified.

Its role includes:

- verifying that required migrated Tomcat source slices exist;
- compiling the current Java bridge and transport surface;
- running the `SocketWrapperBase` / `SocketProcessorBase` surface test;
- running JNI and native runtime regression tests;
- performing strict native compilation.

A successful `build_test.xml` run must not be described as a successful full Tomcat formal build.

## Current policy

1. Development changes may be verified first with `ant -f build_test.xml verify`.
2. `build.xml` is the target that will eventually perform the complete repository build.
3. The two entry points must remain semantically distinct; verification targets must not silently become the formal build.
