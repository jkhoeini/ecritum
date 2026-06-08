# Performance And Artifact Budgets

ADR-025 is the source of truth for the next-release single-default-artifact
policy. ADR-018 remains the historical M1-M8 Core/Full budget record. This page
is the operator-facing checklist.

## Current Commands

```sh
mise exec -- just size
mise exec -- just bench-cold-start
mise exec -- just bench-swift-cold-start
mise exec -- just bench-idle-rss
mise exec -- just bench-first-eval
mise exec -- just check-dep-delta
```

These commands record the current single default artifact path. Historical M8
Core/Full measurements remain below for release-history context only.

`bench-first-eval` compiles a tiny C host against the packaged XCFramework and
measures `eval_start` through `job_result` for SCI Clojure. It exits nonzero when
the packaged artifact is missing or when first-eval latency exceeds budget.

## Next Default Artifact Measurements

The next release is blocked until these measurements are recorded for the single
default artifact and copied into the README/release evidence:

| Measurement | Command family | Required evidence |
| --- | --- | --- |
| Hosted zip size | `package-artifact`, `checksum` | release zip bytes and SwiftPM checksum |
| Unzipped framework size | `size` | `EcritumRuntime.framework` size |
| App bundle delta | packaged app smoke | app size with and without Ecritum |
| Cold start | `bench-cold-start` | p50 and p95 |
| First eval per language | `bench-first-eval` | Clojure, JavaScript, Lua, Python, Ruby |
| Idle RSS | `bench-idle-rss` | post-load and post-eval RSS |
| Dependency/license delta | `check-dep-delta`, `license-report-strict` | reviewed delta and SPDX inventory |
| Resource inventory | `inspect` | bundled runtimes and resource directories |

## Historical v0.1.0/M8 Baselines

| Gate | Command | Budget | Current baseline |
| --- | --- | ---: | ---: |
| Artifact directory | `just size ... core` | 35,000,000 bytes | 31,144,742 bytes |
| Artifact warning | `just size ... core` | 33,000,000 bytes or >10% growth | 31,144,742 bytes |
| Public wrapper | `just size ... core` | 262,144 bytes | 147,280 bytes |
| Private runtime | `just size ... core` | 33,000,000 bytes | 30,511,744 bytes |
| C host cold start | `just bench-cold-start` | p50 250 ms, p95 500 ms | measured per run |
| `dlopen+dlsym` | `just bench-cold-start` | p95 250 ms | measured per run |
| First wrapper call | `just bench-cold-start` | p95 750 ms | measured per run |
| Swift host cold start | `just bench-swift-cold-start` | p50 1,000 ms, p95 2,000 ms | measured per run |
| First eval | `just bench-first-eval` | p50 500 ms, p95 1,000 ms | measured per run |
| Idle RSS | `just bench-idle-rss` | 75 MiB for M1 version runtime | measured per run |
| Dependency/license delta | `just check-dep-delta` | no unreviewed delta | M1 baseline |

## Historical v0.1.0/M8 Core vs Full Criteria

The v0.1.0 release used Core as the default SwiftPM artifact and kept Full as an
explicit heavier lane. ADR-025 supersedes this as future product policy, but the
M8 commands and historical release notes still use those names.

Historically, the Core artifact is a SCI/Clojure-only Native Image build. The
combined SCI/GraalJS/Lua artifact is a Full candidate and must use explicit Full
paths and release-lane metadata.

Initial M7 Full-lane size gates for that candidate are:

| Gate | Command | Budget | Full baseline |
| --- | --- | ---: | ---: |
| Artifact directory | `just size ... full` | 200,000,000 bytes | 151,941,677 bytes |
| Artifact warning | `just size ... full` | 175,000,000 bytes or >10% growth | 151,941,677 bytes |
| Public wrapper | `just size ... full` | 262,144 bytes | measured per artifact |
| Private runtime | `just size ... full` | 190,000,000 bytes | measured per artifact |

Under ADR-025, Python and Ruby are default-artifact candidates, not separate
heavy-lane product artifacts. They still cannot be claimed until measurements
prove:

- every shipped runtime license is known and inventoried
- strict conformance and strict abuse suites pass with zero required pending
  cases
- zip size, unzipped framework size, app bundle delta, cold start, first eval,
  idle RSS, dependency delta, and resource inventory are recorded
- no host or end user installs a separate language runtime
- package managers, third-party packages, native extensions, and mutable package
  caches are not enabled

JavaScript and Lua are already implemented in the heavier local lane. M10 owns
promoting Clojure, JavaScript, and Lua into the single default artifact path.
