# ADR-018: Performance and Artifact Budget Policy

Status: Accepted; next-release artifact policy superseded by ADR-025.

ADR-025 supersedes Core-vs-Full budgeting for M9 and later. This ADR remains the
historical source for M1-M8 gates. The next release uses single-default-artifact
measurements and must record zip size, unzipped framework size, app bundle
delta, cold start, first eval per language, idle RSS, dependency delta,
license/SBOM inventory, and resource inventory before publication.

## Context

Ecritum will add language runtimes incrementally. Each runtime can increase
artifact size, startup latency, memory footprint, dependency surface, and license
risk. M1 needs numeric tripwires before Python and Ruby work begins, even though
the current runtime only exports a version smoke.

## Decision

Ecritum tracks Core and Full artifact lanes.

Core is the default SwiftPM artifact. It must stay small enough for ordinary
desktop app distribution and must not require a separate GraalVM, JDK, Python,
Ruby, Node, or Clojure install at runtime. Full is allowed to carry heavier
language runtimes once licensing, packaging, and performance data justify it.
The Full lane is explicit; it is never the default artifact by accident.

Current Core gates:

- artifact directory size: 35,000,000 bytes
- artifact size warning: above 33,000,000 bytes or above 10% growth from the
  current Core baseline
- public wrapper binary size: 262,144 bytes
- private native runtime size: 33,000,000 bytes
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

M7 adds absolute Full-lane packaging gates for the current combined
SCI/GraalJS/Lua candidate artifact:

- artifact directory size: 200,000,000 bytes
- artifact size warning: above 175,000,000 bytes or above 10% growth from the
  Full-lane baseline
- public wrapper binary size: 262,144 bytes
- private native runtime size: 190,000,000 bytes
- Full-lane artifact baseline: 151,941,677 bytes

M8 adds a true SCI-only Core artifact lane. The Core Native Image profile exports
only the private Clojure entrypoints, the wrapper is compiled with Core language
guards, and Core dependency/SBOM evidence excludes GraalJS, Truffle, and LuaJ.
The first measured Core artifact exceeds the original M1 tripwires, so this ADR
accepts a new default-lane budget based on the measured SCI-only artifact:

- Core artifact baseline: 31,144,742 bytes
- Core private native runtime baseline: 30,511,744 bytes
- Core public wrapper baseline: 147,280 bytes

Python and Ruby are Full-only until GraalPy and TruffleRuby measurements prove
they satisfy Core gates with known licenses and without separate runtime
installation. SCI/Clojure is the default Core language. JavaScript and Lua are
Full-only unless later measurements and security work justify promoting them.

## Measurement Commands

- `just size [artifact] [core|full]`: artifact, wrapper, and private runtime
  size JSON for the selected lane. The default lane is Core.
- `just bench-cold-start`: C host process, `dlopen+dlsym`, and first wrapper
  call p50/p95 JSON over repeated fresh-process runs.
- `just bench-swift-cold-start`: Swift host p50/p95 JSON after the example is
  built.
- `just bench-idle-rss`: RSS after `dlopen` and after first wrapper call JSON.
- `just bench-first-eval`: explicit `not_applicable` JSON until an eval API
  exists.
- `just check-dep-delta`: dependency/license inventory delta JSON.
- `just release-check [core|full]`: includes release gates and strict
  shipped-license blocking for the selected lane. The default lane is Core.

## M1 Baseline

Historical M1 artifact measurements:

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
