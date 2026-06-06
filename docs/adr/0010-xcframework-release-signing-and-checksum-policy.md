# ADR-010: XCFramework Release, Signing, And Checksum Policy

Status: Accepted

Reviewers: Release, Swift DX, Unix, Tests/TDD, Security, GraalVM Runtime,
Claude CLI attempted.

## Context

ADR-001 chose SwiftPM plus a prebuilt `EcritumRuntime.xcframework` as the
consumer distribution shape. SwiftPM remote binary targets require a URL and
checksum in `Package.swift`, and Swift documents `swift package
compute-checksum` as the command for binary artifact checksums:
https://docs.swift.org/swiftpm/documentation/packagemanagerdocs/packagecomputechecksum/

Apple's macOS distribution guidance requires Developer ID signing and
notarization for software distributed outside the Mac App Store. Apple documents
notarization as a scan and code-signing check before public distribution, and
current command-line notarization uses `notarytool`, not the retired `altool`:
https://developer.apple.com/documentation/security/notarizing_macos_software_before_distribution
and
https://developer.apple.com/documentation/technotes/tn3147-migrating-to-the-latest-notarization-tool

The current repo can assemble and zip a local macOS arm64 XCFramework, but before
M7-001 it did not force release-manifest URL/checksum mode, did not persist
checksum evidence, and did not fail on invalid local framework signatures. The
current combined SCI/GraalJS/Lua local artifact also exceeds ADR-018 Core size
budgets, so this ADR defines release mechanics without declaring the current
artifact shippable as Core.

This ADR owns the XCFramework-specific release contract: slice policy, framework
layout, manifest mode, code-signing state, and SwiftPM checksum use. ADR-017
owns deterministic packaging evidence and provenance for the resulting archive.
ADR-011 and ADR-015 own license, SBOM, vulnerability response, and revocation.

## Decision

Ecritum has two artifact lanes:

- Core: the default SwiftPM binary target for normal desktop apps.
- Full: a future larger artifact lane for runtimes that fail Core size or
  startup gates but pass licensing, security, and clean-consumer checks.

Release artifacts use this archive shape:

```text
EcritumRuntime.xcframework.zip
  EcritumRuntime.xcframework/
```

The package manifest used for public release must resolve
`EcritumRuntime` through `.binaryTarget(name:url:checksum:)`. A public tag must
not resolve the runtime target through `.binaryTarget(path:)`. Release
automation must set `ECRITUM_RELEASE_RUNTIME_REQUIRED=1` plus
`ECRITUM_RUNTIME_URL` and `ECRITUM_RUNTIME_CHECKSUM`; manifest evaluation fails
when release mode is required but either value is missing. Setting only one of
URL or checksum is also a manifest error.

`dist/local/EcritumRuntime.xcframework` remains a local development artifact.
It may use ad-hoc signing for local smoke tests. `just xcframework` signs the
nested private runtime dylib first and the framework bundle second, then
`just check-xcframework` verifies both.

Public release artifacts require:

- Developer ID signing with a non-ad-hoc identity.
- Hardened runtime options unless a later signing ADR documents a specific
  exception.
- Notarization through `xcrun notarytool`, with failure logs retained in release
  evidence.
- Stapling or an explicit documented reason stapling is not applicable to the
  chosen binary-target archive format.
- `codesign --verify --verbose=2` on every shipped framework slice and nested
  dylib after signing and after packaging/unpacking.

`scripts/check-public-signing.py` is the public signing/notarization gate. It
validates every shipped `EcritumRuntime.framework/EcritumRuntime` executable and
nested `Resources/*.dylib` with `codesign --verify --deep --strict
--verbose=2`, then reads `codesign -dv --verbose=4` evidence to reject ad-hoc
signatures, non-Developer ID authority chains, missing team identifiers, missing
hardened runtime, missing secure timestamps, and `get-task-allow=true`
entitlements. The same validation runs on the unpacked release zip, not just the
prepared `dist/<lane>` artifact.

The public gate requires `notarytool submit` and `notarytool log` JSON evidence
for the exact release zip SHA-256. Both submit and log evidence must resolve to
status `Accepted`, must reference the same submission/job id, and the log must
contain no error-severity issues. Because SwiftPM binary targets use zip
archives and Apple documents that zip archives cannot be stapled directly, the
gate accepts an explicit JSON stapling exception for zip artifacts. The exception
must record `artifactFormat=zip`, reason, accepted-by, accepted date, ADR/source,
notarization submission id, and the exact release zip SHA-256. If a later
release format can be stapled directly, the gate must use stapler validation
evidence instead of the zip exception.

Local `release-check` remains contributor-friendly and may validate ad-hoc
signed development artifacts. Public release mode is explicit:
`release-check.sh --public` or `just release-check-public`. Public mode requires
the signing/notarization evidence above and a hosted HTTPS
`.binaryTarget(url:checksum:)` consumer smoke; skipped clean-consumer evidence
cannot satisfy publication.

`just checksum` prints the SwiftPM checksum for the current release zip.
`just package-artifact` writes the zip, a JSON package manifest, and a `.checksum`
sidecar. `just release-check` persists the checksum to
`build/release/swiftpm-checksum.txt` and records release-manifest JSON produced
with `ECRITUM_RELEASE_RUNTIME_REQUIRED=1`.

macOS arm64 is the only implemented slice in this task. x86_64 and universal
macOS artifacts are phased support because ADR-008 and ADR-009 recorded that the
current GraalVM 25 line removed macOS x64 support. Any future x86_64 lane needs
a separate toolchain decision, slice-specific verification, and release
evidence before M7 can claim universal desktop coverage.

Mach-O `current_version` and `compatibility_version` remain `0.1.0` for
pre-release artifacts. Before ABI v1 release-candidate status, ADR-012 requires
the release pipeline to tie Mach-O compatibility metadata to the ABI major line
or document why the artifact remains development-only.

## Consequences

Release checks can prove that local artifacts are shaped, signed, checksummed,
and manifest-selectable as remote binary targets without uploading a real public
zip. They do not prove hosted SwiftPM resolution from a clean consumer project;
that remains M7-002.

The current artifact is not publishable as Core until the ADR-018 size blocker
is resolved or a new ADR reclassifies it into a Full lane with separate naming
and expectations.

Developer ID credentials are intentionally not required for contributor builds.
The public release job must provide signing identity, notarization credentials,
artifact URL, and checksum from trusted release metadata.

## Verification Plan

M7-001 verifies this ADR with:

- `mise exec -- just xcframework`
- `mise exec -- just check-xcframework`
- `mise exec -- just inspect`
- `mise exec -- just package-artifact`
- `mise exec -- just package-artifact-verify`
- `mise exec -- just checksum`
- release-mode `swift package describe --type json` with
  `ECRITUM_RELEASE_RUNTIME_REQUIRED=1`
- `mise exec -- just release-check`, or recorded expected failures for size and
  strict license blockers

M7-002 must add a hosted or local HTTP-style clean-consumer SwiftPM resolution
test that actually consumes `.binaryTarget(url:checksum:)`.
M8-004 adds the public Developer ID signing, notarization, zip-stapling
exception, post-unpack signature verification, and mandatory hosted consumer
gate.
