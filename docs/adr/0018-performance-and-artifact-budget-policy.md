# ADR-018: Performance and Artifact Budget Policy

Status: Accepted

## Context

Ecritum will add language runtimes incrementally. Each runtime can increase
artifact size, startup latency, memory footprint, dependency surface, and license
risk. M1 needs numeric tripwires before Python and Ruby work begins, even though
the current runtime only exports a version smoke.

## Decision

Ecritum tracks a Core artifact and a future Full artifact.

Core is the default SwiftPM artifact. It must stay small enough for ordinary
desktop app distribution and must not require a separate GraalVM, JDK, Python,
Ruby, Node, or Clojure install at runtime. Full is allowed to carry heavier
language runtimes once licensing, packaging, and performance data justify it.

Initial Core gates:

- artifact directory size: 25,000,000 bytes
- artifact size warning: above 15,000,000 bytes or above 10% growth from the
  current baseline
- public wrapper binary size: 262,144 bytes
- private native runtime size: 20,000,000 bytes
- C host fresh-process cold start: p50 250 ms, p95 500 ms
- `dlopen + dlsym`: p95 250 ms
- first wrapper call, including Graal isolate create/call/teardown: p95 750 ms
- Swift host fresh-process cold start after build: p50 1,000 ms, p95 2,000 ms
- first eval: p50 500 ms, p95 1,000 ms, explicitly not applicable in M1 and
  enforced no later than M3 when an eval ABI exists
- idle RSS after first wrapper call: 75 MiB for the M1 version-only runtime;
  Core language runtime RSS warning starts above 120 MiB
- dependency/license delta: no unreviewed shipped, build, or test dependency
  changes; no new shipped component without SPDX inventory or scope

Initial Full gates before Python or Ruby can move into Core:

- same-commit artifact delta versus Core: no more than 80,000,000 bytes
- cold-start p95 delta versus Core: no more than 500 ms
- first-eval p95 delta versus Core: no more than 750 ms
- idle RSS delta versus Core: no more than 150 MiB
- every shipped runtime has known redistributable license terms and SPDX
  inventory
- conformance suite passes for every included language
- the runtime remains bundled inside Ecritum's artifact; hosts and end users do
  not install separate language runtimes

Python and Ruby are Full-only until GraalPy and TruffleRuby measurements prove
they satisfy Core gates with known licenses and without separate runtime
installation. SCI/Clojure, JavaScript, and Lua may remain Core only while their
measured deltas stay within Core gates.

## Measurement Commands

- `just size`: artifact, wrapper, and private runtime size JSON.
- `just bench-cold-start`: C host process, `dlopen+dlsym`, and first wrapper
  call p50/p95 JSON over repeated fresh-process runs.
- `just bench-swift-cold-start`: Swift host p50/p95 JSON after the example is
  built.
- `just bench-idle-rss`: RSS after `dlopen` and after first wrapper call JSON.
- `just bench-first-eval`: explicit `not_applicable` JSON until an eval API
  exists.
- `just check-dep-delta`: dependency/license inventory delta JSON.
- `just release-check`: includes release gates and strict shipped-license
  blocking; first-eval remains outside release-check until eval exists.

## M1 Baseline

Current M1 artifact measurements:

- artifact directory: 12,967,170 bytes
- public wrapper binary: 33,568 bytes
- private native runtime: 12,931,448 bytes
- startup/RSS baselines are measured per run by the benchmark commands because
  they vary by machine load and CPU power state
- dependency baseline:
  - shipped: `EcritumRuntime.xcframework`, GraalVM Native Image embedded runtime
    code
  - build: `org.graalvm.sdk:nativeimage`, `org.graalvm.sdk:word`
  - test: `org.junit.jupiter:junit-jupiter`

## Consequences

Budget values are early tripwires, not final product promises. M1 values are
intentionally conservative and must be revisited after SCI and GraalJS data.
Missing eval support is explicit `not_applicable` in M1; later runtime work
cannot claim eval budget coverage until the public eval API exists and
`just bench-first-eval` measures it.
