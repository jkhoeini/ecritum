# Ecritum Debt Ledger

Accepted shortcuts, open risks, and temporary scaffolding live here. A task is
not done if it introduced debt without an entry.

## Entry Template

- ID:
- Source task:
- Introduced by:
- Owner persona:
- Date:
- Impact:
- Reason accepted:
- Resolve-by phase:
- Exit condition:
- Removal task:
- Verification required:

## Active Debt

- ID: ECRITUM-DEBT-0002
- Source task: M1-002/M1-003
- Introduced by: SwiftPM and Native Image scaffold
- Owner persona: TDD, Testability, and Verification Engineer
- Date: 2026-06-05
- Impact: Status codes are duplicated in `ecritum.h` and `EcritumStatus.java`.
  Drift would silently break cross-language error interpretation.
- Reason accepted: The scaffold needs a tiny status surface before the full C ABI
  ownership/error ADR and ABI manifest tooling exist.
- Resolve-by phase: M2-001
- Exit condition: Status codes are covered by the accepted C ABI ownership/error
  ADR and enforced by either generated constants, a checked ABI manifest, or a
  cross-language verification test.
- Removal task: M2-001 ADR for C ABI ownership, lifecycle, and errors
- Verification required: ABI/status-code check in `mise exec -- just test` or
  `mise exec -- just check-abi`.

- ID: ECRITUM-DEBT-0004
- Source task: M1-007
- Introduced by: Initial performance and artifact budget policy
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-05
- Impact: M1 budget values are based on the version-only Native Image runtime
  before SCI, GraalJS, Lua, Python, or Ruby are embedded.
- Reason accepted: Numeric tripwires are needed before language-runtime work can
  proceed, but final product budgets require runtime-specific measurements.
- Resolve-by phase: M4.5
- Exit condition: SCI and GraalJS size/startup/RSS/first-eval data have been
  recorded and ADR-018 budgets are either reaffirmed or revised.
- Removal task: M4.5 ABI freeze and packaged app smoke budget review
- Verification required: `mise exec -- just perf-baseline` and documented
  language-runtime delta table.

- ID: ECRITUM-DEBT-0005
- Source task: M1-007
- Introduced by: Initial Core vs Full artifact split policy
- Owner persona: GraalVM and Polyglot Runtime Engineer
- Date: 2026-06-05
- Impact: Core/Full thresholds for Python and Ruby are empirical hypotheses
  until GraalPy and TruffleRuby artifacts are measured.
- Reason accepted: The split criteria must exist before Python/Ruby work starts,
  but the measurements cannot exist until those runtime spikes are implemented.
- Resolve-by phase: M6
- Exit condition: GraalPy and TruffleRuby measurements decide whether each
  runtime remains Full-only or can satisfy Core gates.
- Removal task: M6 GraalPy/TruffleRuby measurement and artifact policy review
- Verification required: `mise exec -- just perf-baseline`, strict license gate,
  and conformance smoke for each measured runtime.

## Resolved Debt

- ID: ECRITUM-DEBT-0001
- Source task: M1-002
- Introduced by: SwiftPM scaffold
- Owner persona: Swift API and Developer Experience Engineer
- Date: 2026-06-05
- Resolved in: M1-004
- Resolution: Removed the weak `ecritum_version` fallback from
  `Sources/CEcritum/shim.c`; SwiftPM runtime tests now require the packaged
  `EcritumRuntime.xcframework` to provide the public C symbol.
- Verification: `mise exec -- just test-swift-scaffold` proves the scaffold
  path without a runtime artifact, and `mise exec -- just test-swift` proves the
  packaged runtime path with no skipped runtime tests.

- ID: ECRITUM-DEBT-0003
- Source task: M1-002
- Introduced by: SwiftPM scaffold
- Owner persona: Swift API and Developer Experience Engineer
- Date: 2026-06-05
- Resolved in: M1-004
- Resolution: `scripts/swift-test.sh` records a content hash of runtime artifact
  availability in `build/swift-test/ecritum-runtime-artifact-state`, exports an
  explicit `ECRITUM_LOCAL_RUNTIME` manifest mode, and resets SwiftPM package
  state when the mode or artifact content changes.
- Verification: `mise exec -- just test-swift-scaffold` passed with the artifact
  absent, `mise exec -- just test-swift` passed with the artifact present, and
  `mise exec -- just test` passed with the artifact present.
