# Tomcat Java Source Migration Rule

This is a mandatory NativeTomcat engineering rule.

## Rule

Whenever implementation, planning, debugging, or verification materially involves a Tomcat Java source file, **first check whether the exact file already exists in this repository** under `java/` at the path implied by its original Tomcat package.

- If the file exists: use and inspect the repository copy; do not create a duplicate.
- If it does not exist: immediately migrate the exact pinned-upstream source file to the corresponding `java/<package path>/` location.
- Preserve the original package and, for migrated upstream source, preserve the upstream implementation rather than reconstructing it from memory or API documentation.
- After every migration or material change, update **both `build.xml` and `build_test.xml`** so the file is explicitly included in the appropriate formal-build and verification paths.
- Apply the same rule recursively to Tomcat supporting classes discovered while resolving dependencies.
- A compatibility shell is permitted only as a temporary, explicitly labelled checkpoint. Once exact upstream source is available, compare it method-by-method and replace the shell incrementally.

## Required integration order

1. `SocketWrapperBase` compatibility shell
2. Method-by-method comparison with pinned upstream `SocketWrapperBase`
3. Migration of the actual Tomcat supporting classes required by those methods
4. One-by-one replacement of `NativeSocketWrapper` deferred methods
5. Verification against `NioSocketWrapper`
6. Verification against `NioEndpoint.Poller` / `processKey`
7. Connection to `SocketProcessorBase`
8. Only then connection to `Http11Processor`

The migration rule applies at every one of these stages.

## Verification rule

An upstream migration may only be called complete when the exact pinned source has been obtained and checked. API documentation alone is not sufficient evidence for implementation equivalence.
