# Performance And Artifact Budgets

ADR-018 is the source of truth for Ecritum's initial performance and artifact
budget policy. This page is the operator-facing checklist.

## Current Commands

```sh
mise exec -- just size dist/core/EcritumRuntime.xcframework core
mise exec -- just size dist/full/EcritumRuntime.xcframework full
mise exec -- just bench-cold-start
mise exec -- just bench-swift-cold-start
mise exec -- just bench-idle-rss
mise exec -- just bench-first-eval
mise exec -- just check-dep-delta core
mise exec -- just check-dep-delta full
```

`bench-first-eval` compiles a tiny C host against the packaged XCFramework and
measures `eval_start` through `job_result` for SCI Clojure. It exits nonzero when
the packaged artifact is missing or when first-eval latency exceeds budget.

## Current Core Gates

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

## Core vs Full Criteria

Core is the default SwiftPM artifact. Full is the heavier artifact reserved for
runtime combinations that exceed Core gates. `just size` defaults to Core;
Full checks must be requested explicitly with the `full` lane.

The Core artifact is a SCI/Clojure-only Native Image build. The combined
SCI/GraalJS/Lua artifact is a Full candidate and must use explicit Full paths
and release-lane metadata.

Initial M7 Full-lane size gates for that candidate are:

| Gate | Command | Budget | Full baseline |
| --- | --- | ---: | ---: |
| Artifact directory | `just size ... full` | 200,000,000 bytes | 151,941,677 bytes |
| Artifact warning | `just size ... full` | 175,000,000 bytes or >10% growth | 151,941,677 bytes |
| Public wrapper | `just size ... full` | 262,144 bytes | measured per artifact |
| Private runtime | `just size ... full` | 190,000,000 bytes | measured per artifact |

Python and Ruby remain Full-only until measurements prove:

- same-commit artifact delta versus Core is no more than 80,000,000 bytes
- cold-start p95 delta versus Core is no more than 500 ms
- first-eval p95 delta versus Core is no more than 750 ms
- idle RSS delta versus Core is no more than 150 MiB
- every shipped runtime license is known and inventoried
- the conformance suite passes
- no host or end user installs a separate language runtime

SCI/Clojure is the default Core language. JavaScript and Lua remain Full-only
unless later measurements and security work justify promoting them.
