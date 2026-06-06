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

- ID: ECRITUM-DEBT-0012
- Source task: M7-001
- Introduced by: Release artifact pipeline hardening
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-06
- Impact: Local XCFramework builds are ad-hoc signed and verified, but public
  Developer ID signing, hardened-runtime enforcement, notarization, stapling or
  stapling exception, and hosted SwiftPM artifact verification are not yet
  enforced by a public release gate.
- Reason accepted: M7-001 hardens deterministic packaging, local signing,
  checksum evidence, and release-manifest URL/checksum selection without
  requiring Apple Developer credentials or public artifact hosting in developer
  workspaces. ADR-010 requires the stricter public gate before publication.
- Resolve-by phase: M7
- Exit condition: Public release automation verifies a Developer ID signed and
  notarized artifact, records notarization evidence, validates the final
  uploaded URL/checksum through SwiftPM, and fails if `.binaryTarget(path:)`
  resolves during public release preparation.
- Removal task: M7 public release signing/notarization and hosted SwiftPM
  consumer gate.
- Verification required: Developer ID `codesign --verify --deep --strict`
  equivalent for every shipped slice and nested dylib, `xcrun notarytool`
  submission evidence, stapler validation or documented non-app archive
  exception, hosted `.binaryTarget(url:checksum:)` consumer smoke, and
  `mise exec -- just release-check`.

## Resolved Debt

- ID: ECRITUM-DEBT-0007
- Source task: M3-002
- Introduced by: Clojure eval and host-call roll-up
- Owner persona: Architecture Expert Engineer
- Date: 2026-06-05
- Resolved in: M8-001
- Resolution: ADR-024 explicitly drops public line/column and stack-frame
  diagnostics from the v0 C ABI support claim. The supported v0 native
  diagnostic surface is status, category, redacted message, operation,
  language, and host-supplied source name. Swift may retain the future-compatible
  `EcritumStackFrame` model, but native-backed v0 errors leave stack frames
  empty until a later additive ABI decision supplies safe frame accessors.
- Verification: M8-001 records ADR/support-claim updates, ABI unchanged
  verification, Swift error tests, artifact-backed C/Swift eval smoke, and
  conformance evidence. Claude CLI review was attempted directly and timed out
  with no output; the timeout is recorded in PROJECT.org.

- ID: ECRITUM-DEBT-0011
- Source task: M5-001
- Introduced by: LuaJ Native Image spike release gate
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-06
- Resolved in: M7-007
- Resolution: ADR-0023 and M5-001 record LuaJ's dependency, license, security,
  size, cold-start, idle-RSS, and first-Lua-eval evidence. M7-006/ADR-018
  classify the current combined SCI/GraalJS/Lua artifact as a Full candidate,
  not release-ready Core. Lua remains experimental/outside Core until a future
  memory-limiting design is accepted, so this classification debt is closed
  without promoting Lua to the default Core artifact.
- Verification: M5-001 records `security-lua`, `bench-lua-first-eval`,
  startup/RSS, size, license, and dependency-delta evidence. M7-006 records
  Full-lane size success and Core-lane size failure for the combined artifact.
  Release persona review confirmed this debt can resolve from existing
  evidence; Claude plan review timed out with no output.

- ID: ECRITUM-DEBT-0009
- Source task: M4-002
- Introduced by: GraalJS smoke path
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-05
- Resolved in: M7-007
- Resolution: ADR-018 now defines explicit Core and Full release lanes. M7-006
  classifies the combined SCI/GraalJS/Lua artifact that includes GraalJS as a
  Full candidate and preserves Core as the default lane until true Core artifact
  production or an ADR-018 rebaseline lands. GraalJS is therefore no longer an
  unresolved implicit Core-size overage.
- Verification: M4-002 records GraalJS conformance/security, size, startup,
  RSS, first-eval, license, and dependency evidence. M7-006 records Full-lane
  size success, Core-lane size failure, lane-aware package metadata, and
  release-check lane propagation. Release persona review confirmed this debt can
  resolve from existing evidence; Claude plan review timed out with no output.

- ID: ECRITUM-DEBT-0006
- Source task: M3-002B
- Introduced by: Embedded SCI eval backend
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-05
- Resolved in: M7-007
- Resolution: ADR-018 now defines the current combined SCI/GraalJS/Lua artifact
  as a Full candidate and keeps Core as the default lane until a smaller Core
  build exists or the budget is rebaselined. SCI's initial Core-size overage is
  no longer unresolved policy debt; it is part of the explicit Full candidate
  classification.
- Verification: M3 and M7 verification records SCI runtime behavior,
  conformance/security, size, first-eval/startup/RSS, license, and dependency
  evidence. M7-006 records lane-aware size policy. Release persona review
  confirmed this debt can resolve from existing evidence; Claude plan review
  timed out with no output.

- ID: ECRITUM-DEBT-0004
- Source task: M1-007
- Introduced by: Initial performance and artifact budget policy
- Owner persona: Release, Licensing, and Distribution Engineer
- Date: 2026-06-05
- Resolved in: M7-007
- Resolution: ADR-018 has been revised from a single early M1 tripwire set into
  explicit Core and Full artifact budgets. The roadmap records measured SCI,
  GraalJS, and Lua size/performance data, Full-lane acceptance for the current
  combined artifact, and the remaining true Core production follow-up.
- Verification: M7-006 records Core/Full size-gate behavior and release-lane
  evidence; M5 and M4 records language-specific performance data. Release
  persona review confirmed this debt can resolve from existing evidence; Claude
  plan review timed out with no output.

- ID: ECRITUM-DEBT-0008
- Source task: M3-003
- Introduced by: First standard-library facades
- Owner persona: Clean Code and Functional Core Engineer
- Date: 2026-06-05
- Resolved in: M4.5-004
- Resolution: Clojure standard-library seams now have dedicated internal
  modules. Literal supported `require` rewriting lives in
  `SciRequirePreprocessor`/`SciRequireRewrite`; SCI/Clojure value normalization
  lives in `ClojureValueCodec`; and `StandardLibraryBridge` returns explicit
  `StandardLibraryResult` success/failure values that Clojure, JavaScript, and
  Lua stdlib call sites unwrap deliberately. No public C ABI, Swift API,
  runtime policy, support claim, or distribution behavior changed.
- Verification: M4.5-004 records focused Java tests for require preprocessing,
  value normalization, bridge result mapping, and facade failure propagation;
  full `test-java`; full `test-m3-003`; Clojure facade conformance/security
  evidence; and Clean Code/TDD persona reviews. Claude CLI reviews were
  attempted directly and timed out with no output; the timeout is recorded in
  PROJECT.org.

- ID: ECRITUM-DEBT-0010
- Source task: M4-002
- Introduced by: GraalJS smoke path
- Owner persona: Clean Code and Functional Core Engineer
- Date: 2026-06-05
- Resolved in: M4.5-003
- Resolution: JavaScript host callback return values now materialize host
  lists/arrays as real guest JavaScript arrays, host maps as null-prototype
  guest JavaScript objects, and byte data as `Uint8Array` before returning to
  guest code. Nested host-returned arrays, objects, and data now normalize
  correctly inside guest-created JavaScript objects and arrays. Unsupported host
  values and cyclic host collections remain rejected. The planned separate
  `JavaScriptValueCodec` extraction was not required; Clean Code review
  accepted the narrower internal boundary fix because it avoids broad proxy
  normalization and does not alter public ABI/API or support claims.
- Verification: M4.5-003 records Java regression tests, native C smoke and ASan
  smoke, rebuilt Native Image and XCFramework artifacts, Swift artifact-backed
  tests, JavaScript conformance/security gates, ABI check, and Clean
  Code/TDD persona reviews. Claude CLI reviews were attempted directly and
  timed out with no output; the timeout is recorded in PROJECT.org.

- ID: ECRITUM-DEBT-0005
- Source task: M1-007
- Introduced by: Initial Core vs Full artifact split policy
- Owner persona: GraalVM and Polyglot Runtime Engineer
- Date: 2026-06-05
- Resolved in: M6-001 and M6-002
- Resolution: ADR-008 gates GraalPy as a Full-artifact candidate and rejects
  Python inclusion in the default Core artifact for v0 based on official
  resource packaging guidance, local Maven dependency-size evidence, and
  unresolved sandbox requirements. ADR-009 gates TruffleRuby as a Full-artifact
  candidate and rejects Ruby inclusion in the default Core artifact for v0 based
  on unavailable GraalVM-25.0.2 Ruby Maven coordinates, local Ruby/LLVM
  dependency-size evidence, bundled-resource inventory risks, and unresolved
  sandbox requirements. Future Python/Ruby implementation proof gaps now live in
  ADR-008 and ADR-009 rather than active debt.
- Verification: M6-001 and M6-002 verification in PROJECT.org records Maven
  dependency probes, local cache and JAR byte-size measurements, support-claim
  scans, unchanged dependency/license gates, and expected ADR-018 size failure
  for the current non-Python/non-Ruby artifact.

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
