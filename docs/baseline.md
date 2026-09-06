# Baseline

## Repository state

NativeTomcat was verified as an empty GitHub repository before bootstrap. The first repository commit is the project scaffold.

## Upstream baseline

The project specification selects these baselines:

| Component | Baseline | Verification status |
|---|---|---|
| Apache Tomcat | 11.0.25 | tag selected; source mapping to be completed before implementation claims |
| Jakarta Servlet | 6.1 | specification baseline |
| NGINX | 1.30.4 | stable reference selected; source mapping to be completed |
| OpenJDK / HotSpot | JDK 25 | LTS/production baseline selected; exact build to be pinned on the build host |

## Evidence policy

Implementation decisions must distinguish source-code facts, specification requirements, design decisions, theoretical analysis and measurements. An unverified version, path, function, API or benchmark must not be presented as fact.

## Initial blocker

The execution environment available to the coding agent cannot resolve GitHub from the local shell, so a local `git clone`/`git ls-remote` cannot currently be used. Repository operations are therefore performed through the GitHub integration, while upstream source verification is limited to sources successfully retrievable through the available source interfaces. Before claiming a complete upstream mapping or running the full Ant/TCK/benchmark pipeline, the build host must have the required upstream source and dependencies available.
