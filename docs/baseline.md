# Baseline

## Repository state

NativeTomcat was verified as an empty GitHub repository before bootstrap. The repository now contains the initial project scaffold and native runtime prototype.

## Upstream baseline

| Component | Baseline | Verification status |
|---|---|---|
| Apache Tomcat | 11.0.25 | verified source ref `11.0.25`; source mapping recorded in `docs/source-mapping.md` |
| Jakarta Servlet | 6.1 | verified through Tomcat 11.0.25 Servlet API/documentation |
| NGINX | 1.30.4 | verified source ref `release-1.30.4`; event source inspected |
| OpenJDK / HotSpot | JDK 25 | selected baseline; build host currently has JDK 21 and therefore cannot yet execute the Tomcat 11.0.25 release build gate |

Tomcat's 11.0.25 build metadata requires Java 17 for compilation and Java 22 or later for the release build, with Apache Ant 1.10.2 or later. The project therefore must not claim a validated Tomcat release build on the current JDK 21 environment.

## Evidence policy

Implementation decisions must distinguish source-code facts, specification requirements, design decisions, theoretical analysis and measurements. An unverified version, path, function, API or benchmark must not be presented as fact.

## Current execution environment

Measured on the available Linux x86-64 coding environment:

| Component | Observed version | Gate |
|---|---|---|
| OS/kernel | Linux 6.18.35 x86_64 | informational |
| C compiler | Debian GCC 14.2.0 | suitable for native prototype |
| Java/Javac | OpenJDK 21.0.11 | **FAIL** for Tomcat 11.0.25 release build requirement (22+) |
| Ant | Apache Ant 1.10.15 | pass for Tomcat's stated minimum |
| OpenSSL | 3.5.5 | available; TLS integration not yet implemented |

Because the Java gate fails, full Tomcat/NativeTomcat Ant integration testing and benchmark execution are blocked on this environment until JDK 22+ is available. The native-only scaffold can be inspected, but it must not be presented as a validated integrated build.

## Local source access limitation

The execution environment available to the coding agent cannot resolve GitHub from the local shell, so a local `git clone`/`git ls-remote` cannot currently be used. Repository operations are performed through the GitHub integration, while upstream source verification is limited to sources retrievable through that interface. Full Ant compilation, integration testing, TCK execution and benchmark execution require a build host with the upstream Tomcat source tree and dependencies available.
