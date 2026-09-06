# NativeTomcat Codex Engineering Rules

## Mandatory Tomcat Java source migration rule

Whenever an implementation, design, debugging task, or verification materially involves a Tomcat Java source file:

1. First check whether the exact Java file already exists under this repository's `java/` tree at the path implied by its original Tomcat package.
2. If it already exists, inspect and use the repository copy; do not create a duplicate.
3. If it does not exist, immediately migrate the exact pinned-upstream file into `java/`, preserving its original package/path and upstream source content. Do not hand-recreate an implementation from memory or from API documentation.
4. After adding or changing such a file, update both `build.xml` and `build_test.xml` as required so the file has an explicit formal-build and verification path.
5. This rule applies to every step of Tomcat integration, including supporting classes discovered while resolving dependencies.
6. A compatibility shell may exist only as an explicitly documented temporary checkpoint. Once the corresponding upstream source is available, compare it method-by-method and replace the shell incrementally with the pinned implementation.

## Required implementation order

`SocketWrapperBase shell -> upstream method audit -> supporting classes -> NativeSocketWrapper deferred methods -> NioSocketWrapper -> NioEndpoint.Poller/processKey -> SocketProcessorBase -> Http11Processor`.

Do not skip ahead merely because a later component can be mocked. Every Tomcat Java class materially touched by a later step is subject to the migration rule above.

## Verification

Never claim an upstream migration is complete unless the exact pinned source was obtained and checked. Distinguish source verification, compilation, unit tests, integration tests, TCK results, and benchmarks.
