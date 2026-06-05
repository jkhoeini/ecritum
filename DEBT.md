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

- ID: ECRITUM-DEBT-0008
- Source task: M3-003
- Introduced by: First standard-library facades
- Owner persona: Clean Code and Functional Core Engineer
- Date: 2026-06-05
- Impact: The Clojure facade behavior is verified through Java/native/Swift/
  conformance/security gates, but some internal boundaries remain too coupled:
  literal `require` preprocessing lives in `SciClojureEvaluator`,
  `StandardLibraryBridge` uses normalized Java values rather than an explicit
  backend-wire result type, and facade functions reuse evaluator normalization.
- Reason accepted: The public ABI remains unchanged, the native boundary uses a
  private stdlib entrypoint, denied-by-default behavior is covered, and Claude's
  final read-only implementation review found no blockers. Splitting these
  internal seams now would be a larger refactor after the M3-003 acceptance gate
  passed.
- Resolve-by phase: M4.5
- Exit condition: Require preprocessing, facade value normalization, and stdlib
  bridge result mapping have dedicated internal modules or an ADR explicitly
  accepts the current coupling.
- Removal task: M4.5 clean-code pass before ABI freeze.
- Verification required: `mise exec -- just test-java`,
  `mise exec -- just test-m3-003`, and a Clean Code persona review confirming
  the internal seams are either decoupled or intentionally accepted.

- ID: ECRITUM-DEBT-0007
- Source task: M3-002
- Introduced by: Clojure eval and host-call roll-up
- Owner persona: Architecture Expert Engineer
- Date: 2026-06-05
- Impact: Script failures expose safe structured status/category/operation/
  language/source-name/message diagnostics, but do not yet expose structured
  script stack-frame accessors through the public C ABI. Swift has a stack-frame
  model, but backend script stack frames remain empty.
- Reason accepted: Raw JVM/Graal/SCI stack traces are unsafe to expose by
  default, and a stable language-neutral public stack-frame ABI needs an ABI
  freeze decision rather than an M3-002 implementation shortcut.
- Resolve-by phase: M4.5
- Exit condition: A public diagnostic stack-frame ABI is accepted and script
  errors can expose redacted language-level frames when available, or the
  roadmap explicitly drops stack-frame diagnostics from the support claim.
- Removal task: M4.5 ABI freeze diagnostic stack-frame review
- Verification required: `mise exec -- just check-abi`,
  artifact-backed C/Swift script-error tests, and conformance evidence for
  source-name plus stack-frame behavior when supported.

- ID: ECRITUM-DEBT-0006
- Source task: M3-002B
- Introduced by: Embedded SCI eval backend
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-05
- Impact: SCI support increases `dist/local/EcritumRuntime.xcframework` to
  29,951,339 bytes and the private Native Image runtime to 29,828,696 bytes,
  exceeding ADR-018's initial M1 Core tripwires of 25,000,000 and 20,000,000
  bytes.
- Reason accepted: M3-002B is a backend integration slice and does not claim
  release-ready Clojure support. The concrete SCI size data is needed before the
  Core budget can be reaffirmed, revised, or split into Core/Full artifacts.
- Resolve-by phase: M4.5
- Exit condition: ADR-018 is revised or reaffirmed with SCI measurements, or
  the SCI artifact is optimized/split until `mise exec -- just size` passes.
- Removal task: M4.5 ABI freeze and packaged app smoke budget review
- Verification required: `mise exec -- just size`,
  `mise exec -- just bench-first-eval`, and documented Core/Full artifact
  decision.

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

- ID: ECRITUM-DEBT-0002
- Source task: M1-002/M1-003
- Introduced by: SwiftPM and Native Image scaffold
- Owner persona: TDD, Testability, and Verification Engineer
- Date: 2026-06-05
- Resolved in: M2-001
- Resolution: ADR-002 accepts the C ABI ownership/error policy, makes the ABI
  manifest the authority for every public status and symbol, and requires future
  M2 status additions to update `docs/abi/ecritum-c-abi.json`, Java status
  constants, Swift mapping, and C/Swift tests together.
- Verification: Current M1 status parity is enforced by
  `mise exec -- just check-abi`; ADR-002 requires the same gate to expand before
  any new M2 ABI status or symbol is exported.

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
