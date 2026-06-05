# ADR-012: ABI Versioning And Compatibility Policy

Status: Accepted

Reviewers: Architecture Expert, Swift DX, Release, Tests/TDD, Security,
Claude CLI.

## Context

Ecritum's stable integration surface is the C ABI in
`Sources/CEcritum/include/ecritum.h`, checked by
`docs/abi/ecritum-c-abi.json` and `just check-abi`. Swift is the primary
developer experience, but Swift, C, and future language wrappers must all bind
the same public ABI without depending on GraalVM generated headers or private
Native Image symbols.

ADR-002, ADR-003, ADR-016, ADR-019, ADR-020, and ADR-021 define the current
runtime, context, value, job, error, host callback, and config semantics. M4 has
now proven the language-neutral eval path with Clojure and JavaScript. M4.5 is
the first point where Ecritum can freeze an ABI candidate before packaged app
smoke work and later runtime additions.

The current public header exposes `ecritum_version`, stable status constants,
opaque handles, length-carrying views, value/job/error/call APIs, and
`ecritum_eval_start`. It does not yet expose an ABI-version or capability query
symbol, so consumers can only use package/artifact version and symbol presence
as compatibility signals.

## Decision

Ecritum ABI v1 is the first frozen public C ABI line. The v1 line is
source-compatible and binary-compatible across patch releases for all symbols,
constants, public structs, typedefs, ownership rules, and error/status semantics
listed in `docs/abi/ecritum-c-abi.json`.

The public C ABI uses only unversioned `ecritum_*` symbol names in v1. Public
symbols must not include GraalVM, JVM, Swift, Clojure, JavaScript, Python, Ruby,
Lua, Native Image, platform-slice, or artifact-shape names. Language support is
selected through data such as the `language` argument to `ecritum_eval_start`
and through runtime configuration, not through language-specific C symbols.

The ABI manifest is the authoritative freeze ledger. Every public C symbol,
status constant, value kind, job state, public typedef, public struct, and
public callback type must be represented in the manifest before the ABI is
declared frozen. `just check-abi` is the required gate for any change that
touches the public header, Java status constants, public Swift status mapping,
or packaged runtime symbols.

The current manifest and `check-abi` implementation are sufficient to guard the
M3/M4 public function and status baseline, but they are not yet sufficient to
declare the ABI frozen. The M4.5 freeze task must expand the manifest schema and
the checker before any release-candidate claim.

## Compatibility Rules

Patch releases on ABI v1 may:

- add new implementation behavior behind existing symbols when the behavior is
  already permitted by the accepted ADRs and remains deny-by-default where
  security-sensitive
- add support for a new script language behind `ecritum_eval_start` when a
  language ADR, conformance, security, size, startup, and license gates pass
- add new Swift-only convenience APIs that compile against the same C ABI
- add new optional public C symbols only after adding a public runtime query
  path that lets older consumers detect support without loading private symbols

Patch releases on ABI v1 must not:

- remove or rename a public `ecritum_*` symbol
- add unmanifested public exports; every exported public wrapper symbol must be
  either listed as public in the manifest or rejected as a leak
- change a public function signature, calling convention, struct layout,
  typedef width, callback signature, ownership rule, or borrowed-view lifetime
- renumber status constants, value kind constants, job state constants, schema
  versions, or existing machine-readable error categories
- change a previously documented success case into a failure, or a previously
  documented failure status into a less specific status, unless a security ADR
  explicitly accepts the tightening
- expose private GraalVM, Native Image, generated-header, or language-adapter
  symbols from the public framework binary
- change Mach-O framework compatibility metadata in a way that claims a broader
  compatibility range than this ADR and the manifest support
- require app developers or end users to install GraalVM, a JDK, Node, Clojure,
  Python, Ruby, Lua, or other language runtimes for the default packaged
  artifact

When Ecritum needs a behavior that cannot satisfy these rules, it must use a new
major ABI line. A major ABI line may keep the `ecritum_*` prefix, but it must
not silently reuse incompatible v1 symbol behavior. The breaking-change ADR must
choose one of:

- add side-by-side versioned symbols such as `ecritum_v2_*`
- ship a separate major-version artifact with an explicit SwiftPM version range
- keep v1 symbols as compatibility shims while exposing new v2 symbols

The breaking-change ADR must include migration examples, deprecation duration,
release notes, and clean-consumer verification for old and new consumers.

## Version And Capability Queries

`ecritum_version` remains the product/runtime version string, not the ABI
compatibility contract. It is useful for diagnostics and release reporting, but
callers must not parse it to infer individual symbol support.

Before Ecritum adds optional public C symbols after the v1 freeze, a follow-up
ADR and implementation task must add an ABI query path. The query path must be
C-compatible, length-safe, and testable from C and Swift. The minimum accepted
shape is:

- a stable ABI major/minor report, where major changes only for incompatible
  ABI lines and minor increases for additive public symbols
- a stable feature/capability query for optional symbols or language runtimes
- `just check-abi` validation that the manifest, header, Swift mapping, and
  artifact symbols agree with the reported ABI metadata

Until that query path exists, M4.5 treats the current public symbol set as
closed for release-candidate purposes. New runtime language support may still be
added behind existing `ecritum_eval_start` because that does not require a new
public symbol.

## Deprecation

Public C ABI v1 symbols are not removed in patch releases. A symbol may be
documented as deprecated only when a replacement exists, both old and new paths
are covered by tests, and the deprecation does not weaken security defaults.

Swift APIs may deprecate faster than C ABI symbols, but Swift deprecations must
continue to compile against the supported C ABI line. SwiftPM package releases
must document the minimum runtime artifact ABI they require.

## Release And SwiftPM Compatibility

The Swift package and binary artifact are versioned together for the default
distribution path. A Swift package release must declare and test the minimum
runtime artifact ABI it supports. A local or release artifact whose ABI is too
old must fail with a typed runtime-unavailable or compatibility error before
attempting eval.

The framework `current_version` and `compatibility_version` values are release
metadata, not substitutes for ABI metadata. Before the first external release
candidate, the release pipeline must either tie those Mach-O values to the ABI
major line or document why the artifact remains pre-ABI-freeze development
output.

Patch releases may update the Native Image implementation, embedded language
dependencies, or Swift wrappers only when:

- `just check-abi` passes
- the public header still compiles as C and C++
- C and Swift contract tests cover the affected public behavior
- artifact inspection shows no private symbol leaks and no extra public exports
- every shipped slice carries the same public header and public ABI symbol set
- size, startup, RSS, first-eval, license, and dependency-delta gates are
  recorded when the artifact contents change

## Freeze-Grade `check-abi`

The M4.5 freeze checker must validate more than the current M3/M4 presence
checks. A freeze-grade `just check-abi` must:

- reject extra public `ecritum_*` exports not listed in the manifest
- verify all packaged slices, not only the local arm64 slice when more slices
  exist
- verify the packaged public header is byte-for-byte the checked source header
- verify all public status constants are present in C, Java, Swift, and the
  manifest with no extra mapped statuses
- verify value kind and job state constants in the public header and manifest
- verify public typedef widths, struct field order, struct field types, and
  callback signatures
- treat private-symbol entries as patterns broad enough to catch new
  `ecritum_graal_*`, `graal_*`, generated-header, or language-adapter leaks
- check Mach-O install names and compatibility metadata against the release
  policy once the release pipeline owns those values
- include negative fixtures that prove the checker fails on extra symbols,
  renumbered constants, missing declarations, private leaks, and header drift

## Verification Requirements

M4.5-001 accepts this ADR only when:

- `mise exec -- just check-abi` passes
- `docs/abi/ecritum-c-abi.json` is reviewed as the freeze ledger for public
  symbols and constants
- reviewer notes confirm whether the current manifest is sufficient for v1 or
  needs a follow-up manifest-schema expansion before ABI freeze
- Claude plan/diff review is run, or the timeout/blocker is recorded

Before M4.5 as a milestone can declare the ABI frozen, a follow-up task must:

- extend the ABI manifest to cover public typedefs, structs, callback types,
  value kinds, job states, and private-symbol patterns, not only status
  constants and function declarations
- harden `just check-abi` into the freeze-grade checker described above
- decide whether an ABI query symbol is required before the first external
  release candidate
- add or verify a compatibility failure path in Swift for runtime artifact ABI
  mismatches
- update release documentation so it no longer describes the ABI manifest as
  version-smoke or M1-only
- run C header C/C++ smoke, C ABI contract tests, Swift tests, packaged
  artifact `check-abi`, sanitizer/leak/stress gates where practical, and a
  clean packaged app smoke

## Consequences

The ABI can evolve through additive runtime behavior without exposing a new
symbol for every language. That keeps the C layer small and language-neutral.

The cost is that optional public C symbols are effectively blocked until an ABI
query path exists. This is intentional: without a query path, adding public
symbols would make SwiftPM/package-version compatibility ambiguous for future
wrappers and binary artifacts.

The current ABI manifest becomes more important than a generated report. If the
manifest misses a public type, constant family, callback type, or private-symbol
pattern, the ABI freeze is not complete even when `just check-abi` passes.

## Alternatives Considered

Semantic version parsing from `ecritum_version` was rejected. Product versions
are useful diagnostics but are too coarse for symbol and feature detection.

Versioned symbol names for every release, such as `ecritum_v1_runtime_create`,
were rejected for v1. They would add noise before Ecritum has a second
incompatible ABI line. Side-by-side versioned symbols remain the preferred
option for a future major break.

Raw `dlsym` feature detection by consumers was rejected as the main contract.
Ecritum can use symbol checks internally in packaging tests, but public
consumers need a stable C query path before optional public symbols become part
of the supported compatibility story.
