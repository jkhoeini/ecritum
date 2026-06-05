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

- ID: ECRITUM-DEBT-0001
- Source task: M1-002
- Introduced by: SwiftPM and C module scaffold
- Owner persona: Release
- Date: 2026-06-05
- Impact: `Sources/CEcritum/shim.c` provides a weak fallback
  `ecritum_version` symbol that only returns `ECRITUM_ERROR_RUNTIME_UNAVAILABLE`.
  This keeps the source package linkable before a local runtime artifact exists,
  but it is not the final public runtime implementation.
- Reason accepted: M1-002 scaffolds the C module before M1-004 assembles and
  links the Native Image runtime artifact.
- Resolve-by phase: M1-004
- Exit condition: `ecritum_version(char *, size_t)` bridges to the packaged
  Native Image entry point, owns Graal isolate setup/teardown internally, and the
  Swift runtime-wrapper test passes through the local XCFramework.
- Removal task: M1-004 Build XCFramework assembly path
- Verification required: `mise exec -- just xcframework`, `mise exec -- just
  test-swift`, symbol inspection proving public `ecritum_version`, and C/Swift
  smoke tests.

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

- ID: ECRITUM-DEBT-0003
- Source task: M1-002
- Introduced by: SwiftPM scaffold
- Owner persona: Swift API and Developer Experience Engineer
- Date: 2026-06-05
- Impact: Swift test tasks run `swift package reset` before test execution. This
  keeps manifest-time binary target switching deterministic while M1 artifacts
  are moving, but it forces full rebuilds during development.
- Reason accepted: M1 still validates local/release artifact switching, and the
  conservative reset avoids stale manifest behavior.
- Resolve-by phase: M1-004
- Exit condition: Swift test tasks invalidate SwiftPM state only when
  `dist/local/EcritumRuntime.xcframework` availability changes, or release
  validation proves reset is unnecessary.
- Removal task: M1-004 Build XCFramework assembly path
- Verification required: Run `mise exec -- just test-swift-scaffold` before the
  local XCFramework exists and `mise exec -- just test-swift` after it exists.
