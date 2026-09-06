# Tomcat Java Source Migration Rule

## 1. Purpose

This is a mandatory NativeTomcat engineering rule. Its purpose is to prevent a compatibility shell, API documentation, or memory-based reconstruction from being mistaken for the pinned Tomcat 11.0.25 implementation.

Pinned upstream commit:

`cbe6e15ee81e2fc6232954292a80cca5d1e84009`

## 2. Mandatory source-first procedure

Whenever implementation, planning, debugging, or verification materially involves a Tomcat Java implementation class:

1. Check whether the exact source already exists in this repository under `java/` at the original package path.
2. If it exists, inspect the repository copy and verify that it is the intended pinned source or an explicitly documented NativeTomcat adaptation.
3. If it does not exist, obtain the exact pinned-upstream file before implementing behavior that depends on it.
4. Preserve the original package/path and, for a migrated upstream file, preserve its implementation rather than reconstructing it from memory or API documentation.
5. Resolve supporting Tomcat classes recursively using the same rule.
6. After a migration or material source change, update both `build.xml` and `build_test.xml` with an explicit compile/verification path.
7. Compile and execute the applicable tests.
8. Record what was actually verified and what remains blocked or deferred.

## 3. Shell rule

A compatibility shell may exist only as an explicitly labelled temporary checkpoint.

A shell may establish a type surface or isolate an independent test, but it must not be described as behaviorally equivalent to Tomcat merely because:

- method names match;
- the code compiles;
- a surface test passes;
- API documentation describes the same public type.

Once the exact pinned source is available, the shell must be compared against it method-by-method and replaced incrementally.

## 4. Required integration order

The transport integration follows this order unless source verification proves that a dependency requires reordering:

1. `SocketWrapperBase` compatibility shell audit;
2. exact pinned `SocketWrapperBase` verification/migration;
3. migration of supporting classes required by that implementation;
4. replacement of `NativeSocketWrapper` deferred transport methods;
5. comparison with exact `NioEndpoint.NioSocketWrapper` behavior;
6. verification of `NioEndpoint.Poller` registration, readiness and `processKey()` mapping;
7. connection through the real `AbstractEndpoint.processSocket()` / `SocketProcessorBase` path;
8. only after the transport path is real, integration with `Http11Processor`;
9. then `CoyoteAdapter` / Catalina / Servlet execution and Servlet 6.1 behavioral tests.

This ordering separates source migration from runtime integration. A source file may be migrated and compile successfully while its NativeTomcat transport integration remains incomplete.

## 5. Evidence levels

Every migration/integration claim must use an explicit level:

- **source-verified** — exact pinned source was obtained and checked;
- **implemented** — repository code contains the intended change;
- **compiled** — the relevant build target succeeds;
- **unit-tested** — focused executable tests pass;
- **integration-tested** — real component boundaries execute together;
- **Servlet/TCK-tested** — application-visible Servlet behavior is tested;
- **benchmark-verified** — measured performance data exists.

Higher levels do not retroactively prove lower-level source equivalence unless the exact source and implementation were also inspected.

Examples:

`compile PASS != Tomcat semantics correct`

`surface test PASS != SocketProcessor integration`

`native event test PASS != Servlet readiness`

`benchmark harness runs != benchmark result`

## 6. Blocked-source rule

If the exact pinned source cannot currently be obtained, materialized, or checked, mark the migration **source-verification incomplete/blocked**.

Do not:

- hand-recreate the class from memory;
- infer implementation behavior from API documentation alone;
- call a shell "migrated";
- advance a dependent integration gate as though the source gate passed.

Independent work that does not depend on the blocked source may proceed, but its scope and independence must be stated.

## 7. Build rule

A migrated Tomcat source file must have:

- a formal compilation path in `build.xml`;
- a verification compilation/test path in `build_test.xml`;
- a source-presence or exact-source verification step where practical.

`build_test.xml` is not a substitute for the upstream Tomcat build. The upstream `upstream/tomcat/build.xml` remains authoritative for the Tomcat distribution itself.

## 8. Final migration criterion

A Tomcat source migration is complete only when all of the following are true:

1. the exact pinned source was obtained;
2. its content/path was checked;
3. required supporting classes were resolved under the same rule;
4. repository code uses that source rather than a compatibility reconstruction;
5. both build paths include it;
6. applicable compilation/tests pass;
7. remaining NativeTomcat-specific semantic differences are explicitly documented.

Source migration completion still does **not** mean Tomcat integration, Servlet compatibility, TCK compliance, or benchmark equivalence has been proven.
