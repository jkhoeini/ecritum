# ADR-007: JavaScript Artifact Classification After GraalJS Measurements

Status: Accepted

## Context

ADR-006 allowed M4-002 to add GraalJS only if the implementation recorded size,
startup, RSS, first-eval, dependency, license, conformance, and security
evidence. ADR-018's initial Core size tripwires were intentionally based on the
pre-language-runtime artifact and required revision after SCI and GraalJS data.

M4-002 measurements for the combined local artifact:

- artifact directory: 150,960,731 bytes
- private Native Image runtime: 150,818,872 bytes
- public wrapper: 130,000 bytes
- startup p95: 74.324 ms
- `dlopen+dlsym` p95: 11.563 ms
- first wrapper call p95: 4.214 ms
- idle RSS p95 after first wrapper call: 15,679,488 bytes
- Clojure first-eval p95: 3.584 ms
- JavaScript first-eval p95: 23.351 ms
- JavaScript conformance: 13 passed, 0 pending
- JavaScript security abuse: 68 passed, 0 pending
- GraalJS dependency delta: reviewed and inventoried

The only new release-policy violation is artifact size. Strict license still
fails on the pre-existing placeholder entries for EcritumRuntime and embedded
Native Image runtime code; GraalJS Maven dependencies have SPDX identifiers.

## Decision

M4-002 may complete as a local JavaScript smoke-path implementation, but it does
not declare the combined GraalJS artifact release-ready Core.

Until M4.5 revises ADR-018 or implements a Core/Full artifact split:

- `just size` is expected to fail for the combined local GraalJS smoke artifact.
- JavaScript remains a measured Core candidate, not a Core release promise.
- Release packaging must either exclude GraalJS from Core, create a Full
  artifact, or accept a revised Core size budget in a follow-up ADR.
- ECRITUM-DEBT-0009 tracks the size blocker.

## Consequences

This keeps M4-002 focused on proving the runtime path and safety envelope while
preventing an accidental product promise that the current 150 MB artifact is the
default release shape.

Downstream roadmap work must resolve artifact classification before ABI freeze
or public release.

## Alternatives Considered

- Rebaseline Core to about 175 MB immediately. Rejected because this is a
  product/distribution decision that needs packaged app evidence, not only a
  local XCFramework smoke.
- Mark JavaScript Full-only immediately. Rejected for M4-002 because no Full
  packaging lane exists yet and the startup/RSS/first-eval data is otherwise
  within current budgets.
- Block M4-002 until artifact splitting exists. Rejected because the smoke path
  produces the data needed to design that split.

## Verification Plan

- `mise exec -- just native`
- `mise exec -- just test-native-eval-smoke`
- `mise exec -- just xcframework`
- `mise exec -- just test-swift`
- `mise exec -- just conformance-javascript-native`
- `mise exec -- just security-javascript`
- `mise exec -- just bench-cold-start`
- `mise exec -- just bench-idle-rss`
- `mise exec -- just bench-javascript-first-eval`
- `mise exec -- just check-dep-delta`
- `mise exec -- just size` must be recorded as expected failing evidence until
  ECRITUM-DEBT-0009 is resolved.

## Reviewers

- Engineering Manager
- Release, Licensing, and Distribution Engineer
- Architecture Expert Engineer
- Claude diff review
