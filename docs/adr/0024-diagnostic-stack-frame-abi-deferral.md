# ADR-024: Diagnostic Stack-Frame ABI Deferral

Status: Accepted

Reviewers: Architecture Expert, Swift DX, Tests/TDD, Security, Claude CLI.

## Context

ADR-002 originally listed source location and stack-frame diagnostics as part of
the future error model. The implemented public C ABI currently exposes only the
safe diagnostic fields that are consistent across SCI Clojure, GraalJS, and
LuaJ:

- status
- category
- redacted message
- operation
- guest language
- source name supplied by the host

The current ABI does not expose line, column, frame count, or borrowed
stack-frame views. Swift has `EcritumErrorDetails.stack` and
`EcritumStackFrame`, but native error copying does not populate them.

Adding public stack-frame accessors now would freeze a language-neutral frame
shape before Ecritum has a shared redaction, source-mapping, truncation, and
cross-runtime semantics for SCI, GraalJS, and LuaJ. Raw backend stack traces are
unsafe because they can expose Java class names, host filesystem paths,
implementation frames, source snippets, or internal runtime details.

## Decision

Ecritum v0 does not claim public diagnostic stack-frame support.

The v0 public C ABI error diagnostic surface is:

- `ecritum_error_status`
- `ecritum_error_category`
- `ecritum_error_message`
- `ecritum_error_operation`
- `ecritum_error_language`
- `ecritum_error_source_name`

Line, column, frame count, and stack-frame accessors are deferred until a later
ABI feature decision defines:

- a redacted language-neutral frame schema
- maximum frame count and string-size limits
- source-name and source-location trust rules
- behavior for runtimes that cannot provide frames safely
- Swift and C migration behavior
- ABI capability detection for optional symbols

Swift may keep `EcritumStackFrame` and the `stack` array on
`EcritumErrorDetails` as future-compatible API. Native-backed v0 diagnostics
leave `stack` empty unless a future ABI explicitly supplies frames.

## Consequences

- ECRITUM-DEBT-0007 is resolved by dropping stack-frame diagnostics from v0
  support claims, not by implementing new C symbols.
- `docs/abi/ecritum-c-abi.json` and `Sources/CEcritum/include/ecritum.h`
  remain unchanged.
- `just check-abi` remains the guard that no unreviewed stack-frame symbols are
  exported.
- The public support claim is narrower but safer: Ecritum exposes redacted
  structured diagnostics with language and source name, not stack frames.
- A future stack-frame ABI must be additive and must follow ADR-012 capability
  detection rules if added after the v1 freeze.

## Verification

- `mise exec -- just check-abi`
- `mise exec -- just test-swift`
- `mise exec -- just test-swift-scaffold`
- `mise exec -- just test-c-abi-eval`
- `mise exec -- just test-xcframework-eval-smoke`
- `mise exec -- just conformance`
