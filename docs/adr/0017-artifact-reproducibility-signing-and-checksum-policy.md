# ADR-017: Artifact Reproducibility, Signing, And Checksum Policy

Status: Accepted

Reviewers: Release, Unix, Tests/TDD, Swift DX, Security, Architecture,
Claude CLI attempted.

## Context

ADR-001 required deterministic packaging, archive checksums, and later
reproducibility policy. ADR-018 added size and dependency gates. M7-001 now
turns those requirements into release evidence.

This ADR owns deterministic archive generation, package manifests, checksums,
and provenance evidence. ADR-010 owns the XCFramework-specific release shape,
slice policy, and public signing/notarization requirements. ADR-011 and ADR-015
own license inventory, SBOM, CVE tracking, vulnerability response, and
revocation.

Reproducibility has two layers:

- Packaging reproducibility: the same prepared XCFramework input produces the
  same zip bytes and SwiftPM checksum.
- Build provenance: the source revision, tools, dependency locks, signing
  identity, and release lane explain what produced the prepared XCFramework.

Signing and notarization can add externally issued metadata. Therefore public
release reproducibility means deterministic packaging of a specific signed input
plus recorded provenance, not a promise that two independent notarization runs
produce byte-identical signed artifacts.

## Decision

`scripts/package-artifact.py` is the canonical archive writer. It must:

- sort archive entries by path
- normalize every zip timestamp to `1980-01-01T00:00:00Z`
- omit `.DS_Store`, AppleDouble `._*`, and `__MACOSX` metadata
- use deterministic permissions from the input files
- write `dist/release/EcritumRuntime.xcframework.zip`
- write `dist/release/EcritumRuntime.xcframework.zip.json`
- write `dist/release/EcritumRuntime.xcframework.zip.checksum`

The package JSON is release evidence. It records:

- format version
- input artifact path
- input artifact digest
- output zip path
- zip SHA-256
- SwiftPM checksum
- checksum sidecar path
- normalized timestamp policy
- archive root
- discovered slices
- per-file path, mode, size, and SHA-256

SwiftPM checksum is treated as the public checksum contract for
`.binaryTarget(url:checksum:)`. The release pipeline also records raw SHA-256
because it is easy to compare outside SwiftPM; M7-001 verifies they match for
the produced zip.

`scripts/check-package-reproducible.py` packages the same input twice into a
temporary directory and fails if:

- zip bytes differ
- SHA-256 values differ
- SwiftPM checksum differs from SHA-256
- checksum sidecars differ from zip bytes
- zip entry order differs
- timestamps are not normalized
- unexpected macOS metadata entries appear
- compression type differs

`just release-check` must run `just package-artifact`,
`just package-artifact-verify`, and `just checksum`, then persist their outputs
under `build/release/`.

Signing order is part of the reproducibility contract. For local ad-hoc builds,
the nested private dylib is signed first and the framework bundle second after
all resources are written. Public Developer ID signing follows the same inside
out order, then notarization evidence is recorded. Packaging happens after
signing so the checksum represents the exact bytes consumers download.

The project does not yet claim clean rebuild reproducibility. Before a public
release, a later release task must add:

- source revision and jj change ID in package provenance
- `mise` tool versions
- Maven dependency digest lock evidence
- Swift toolchain version
- Native Image version
- Core versus Full lane label
- signing identity fingerprint for public releases
- notarization submission ID and status
- SBOM or SPDX output once ADR-015 and ADR-011 are complete

## Consequences

M7-001 can fail stale zip mistakes where `dist/release` contains a previous
archive for a different `dist/local` input. The generated package manifest
contains both input and output hashes, so release upload automation has one
machine-readable file to publish with the zip.

The current package reproducibility check is intentionally independent of the
large Native Image build. It catches archive nondeterminism quickly with the
prepared artifact and has focused Python fixture tests for skip rules,
timestamps, manifest output, and checksum output.

Byte-identical rebuilds from source remain deferred because the native toolchain,
dependency download cache, signing identity, and notarization system need
separate provenance and lock decisions.

## Verification Plan

M7-001 verifies this ADR with:

- `python3 -m unittest Tests/Release/test_package_artifact.py`
- `python3 -m py_compile scripts/package-artifact.py scripts/check-package-reproducible.py`
- `mise exec -- just package-artifact`
- `mise exec -- just package-artifact-verify`
- `mise exec -- just checksum`
- comparing `dist/release/EcritumRuntime.xcframework.zip.checksum` with
  `swift package compute-checksum`
- inspecting `dist/release/EcritumRuntime.xcframework.zip.json`
- `mise exec -- just release-check`, or recording exact expected blockers
