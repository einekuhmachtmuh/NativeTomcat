# Baseline

## Repository state

NativeTomcat was verified as an empty GitHub repository before bootstrap. The repository now contains the initial project scaffold and native runtime prototype.

## Upstream baseline

| Component | Baseline | Verification status |
|---|---|---|
| Apache Tomcat | 11.0.25 | verified source ref `11.0.25`; source mapping recorded in `docs/source-mapping.md` |
| Jakarta Servlet | 6.1 | verified through the official Servlet 6.1 specification/API and Tomcat 11.0.25 build metadata |
| NGINX | 1.30.4 | verified source ref `release-1.30.4`; event source inspected |
| OpenJDK / HotSpot | JDK 17+ initially; exact runtime remains a test variable | relaxed: no fixed JDK 25 requirement unless source inspection or execution demonstrates a compatibility requirement |

Tomcat 11.0.25's `build.xml` declares `compile.release=17`, `min.java.version=17` and `build.java.version=17`. It separately declares `release.java.version=22`, which is a release-build requirement rather than a general Java runtime requirement. Therefore NativeTomcat does not impose JDK 25 as a project-wide baseline. JDK 17+ is the compatibility floor to test, while JDK 22+ is required for the Tomcat 11.0.25 release-build path. fileciteturn65file0L2-L2

Tomcat 11.0.25 also contains an OpenSSL FFM/Panama implementation using `java.lang.foreign` APIs. That implementation is a separate optional integration path and cannot be assumed compatible with every JDK 17+ runtime. NativeTomcat's first C↔Java bridge will therefore use JNI rather than FFM; FFM will remain an optional later path whose minimum JDK is established by actual source/build verification. fileciteturn67file0L2-L2

## Evidence policy

Implementation decisions must distinguish source-code facts, specification requirements, design decisions, theoretical analysis and measurements. An unverified version, path, function, API or benchmark must not be presented as fact.

## Current execution environment

Measured on the available Linux x86-64 coding environment:

| Component | Observed version | Gate |
|---|---|---|
| OS/kernel | Linux 6.18.35 x86_64 | informational |
| C compiler | Debian GCC 14.2.0 | suitable for native prototype |
| Java/Javac | OpenJDK 21.0.11 | pass for Tomcat compilation metadata; **FAIL only for Tomcat 11.0.25 release-build path** |
| Ant | Apache Ant 1.10.15 | pass for Tomcat's stated minimum |
| OpenSSL | 3.5.5 | available; TLS integration not yet implemented |

The current JDK 21 environment is therefore not blocked from compiling code that targets the Tomcat 11.0.25 `compile.release=17` path merely because it is below JDK 22. It is blocked only from claiming a validated Tomcat 11.0.25 release build. FFM-dependent Tomcat components also require separate verification and are not part of the first NativeTomcat bridge.

## Local source access limitation

The execution environment available to the coding agent cannot resolve GitHub from the local shell, so a local `git clone`/`git ls-remote` cannot currently be used. Repository operations are performed through the GitHub integration, while upstream source verification is limited to sources retrievable through that interface. Full Ant compilation, integration testing, TCK execution and benchmark execution require a build host with the upstream Tomcat source tree and dependencies available.
