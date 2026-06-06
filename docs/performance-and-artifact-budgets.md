# Performance And Artifact Budgets

ADR-018 is the source of truth for Ecritum's initial performance and artifact
budget policy. This page is the operator-facing checklist.

## Current Commands

```sh
mise exec -- just size
mise exec -- just size dist/local/EcritumRuntime.xcframework full
mise exec -- just bench-cold-start
mise exec -- just bench-swift-cold-start
mise exec -- just bench-idle-rss
mise exec -- just bench-first-eval
mise exec -- just check-dep-delta
```

`bench-first-eval` compiles a tiny C host against the packaged XCFramework and
measures `eval_start` through `job_result` for SCI Clojure. It exits nonzero when
the packaged artifact is missing or when first-eval latency exceeds budget.

## Initial Core Gates

| Gate | Command | Budget | M1 baseline |
| --- | --- | ---: | ---: |
| Artifact directory | `just size` | 25,000,000 bytes | 12,967,170 bytes |
| Artifact warning | `just size` | 15,000,000 bytes or >10% growth | 12,967,170 bytes |
| Public wrapper | `just size` | 262,144 bytes | 33,568 bytes |
| Private runtime | `just size` | 20,000,000 bytes | 12,931,448 bytes |
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

The current combined SCI/GraalJS/Lua local artifact is a Full candidate. It is
not a release-ready Core artifact, and the default Core lane remains blocked
until a smaller Core build exists or ADR-018 is rebaselined.

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

SCI/Clojure, JavaScript, and Lua may stay Core only while their measured deltas
remain inside the Core gates.
