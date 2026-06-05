# Ecritum Release Gates

This document is the M1 release-gate baseline. It defines the commands a CI job
can run today and the stricter release checks that block publication until
license and distribution work is complete.

## CI Smoke

The CI-ready smoke path is:

```sh
mise trust
mise exec -- just setup
mise exec -- just native
mise exec -- just xcframework
mise exec -- just test
```

`just test` is artifact-aware. Before a local XCFramework exists, it runs the
Swift scaffold tests and Java unit tests. After `just xcframework`, it runs the
Swift runtime path against `dist/local/EcritumRuntime.xcframework` plus the Java
unit tests.

## Release Gate Commands

The M1 release gate is:

```sh
mise exec -- just native
mise exec -- just xcframework
mise exec -- just release-check
```

`just release-check` runs:

- `just test`
- `just check-abi`
- `just check-xcframework`
- `just inspect`
- `just size`
- `just bench-cold-start`
- `just bench-idle-rss`
- `just check-dep-delta`
- `just package-artifact`
- `just license-report`
- `scripts/license-report.py --strict`

`just bench-swift-cold-start` and `just bench-first-eval` are represented by the
M1 budget policy but are not part of `release-check` yet. Swift host timing is
kept out of release-check because it is a host-example benchmark rather than a
release blocker while the C ABI packaging gates cover the artifact runtime path.
First-eval remains explicit `not_applicable` until an eval ABI exists.

The strict license step exits nonzero while shipped licenses remain unknown.
That is intentional: unknown shipped licenses block release publication.

## ABI Gate

`docs/abi/ecritum-c-abi.json` is the checked M1 ABI manifest. `just check-abi`
verifies:

- public status constants in `Sources/CEcritum/include/ecritum.h`
- matching Java status constants in `native/src/main/java/ecritum/EcritumStatus.java`
- public `ecritum_version` declaration
- packaged `_ecritum_version` export when the local XCFramework exists
- absence of private Graal symbols from the public wrapper binary

`ECRITUM-DEBT-0002` still tracks the duplicated C/Java status-code source of
truth. The ABI gate prevents drift until M2 replaces that duplication.

## Inspection And Size

`just inspect` prints JSON with artifact paths, symbols, linked dylibs,
install names, bundled resources, code-signing status, architectures, minimum
macOS version, checksums, and embedded runtime list.

`just size` prints JSON and applies M1 regression budgets:

- artifact directory: 25,000,000 bytes
- artifact warning: above 15,000,000 bytes or above 10% growth from baseline
- public wrapper binary: 262,144 bytes
- private Graal runtime: 20,000,000 bytes

See [performance-and-artifact-budgets.md](performance-and-artifact-budgets.md)
and ADR-018 for startup, first-eval, idle-RSS, dependency-delta, and Core/Full
artifact gates.

## SBOM And License Policy

The chosen SBOM baseline is SPDX 2.3 JSON. `just license-report` emits an SPDX
document with standard document and package annotations for:

- shipped components
- build-only components
- test-only components
- shipped-license blockers

`just license-report` is report-only and exits zero. `just license-report-strict`
and `just release-check` exit nonzero and print release blockers to stderr if
any shipped component has `NOASSERTION`, `UNKNOWN`, or missing license data.

For M1, the strict gate blocks on:

- `EcritumRuntime.xcframework`
- GraalVM Native Image embedded runtime code

The current Maven SDK inputs `org.graalvm.sdk:nativeimage` and
`org.graalvm.sdk:word` are inventoried separately as build-time inputs. JUnit is
inventoried as test-only.

## Reproducibility

M1 uses rebuildable provenance plus deterministic archive metadata:

- tool versions are pinned in `.mise.toml`
- Maven dependency versions are pinned in `native/pom.xml`
- `just native` copies Native Image outputs into `build/native/macos-arm64`
- `just xcframework` assembles `dist/local/EcritumRuntime.xcframework`
- `just package-artifact` writes
  `dist/release/EcritumRuntime.xcframework.zip`
- archive entries are sorted
- archive timestamps are normalized to `1980-01-01T00:00:00Z`
- macOS metadata files such as `.DS_Store`, `._*`, and `__MACOSX` are excluded
- `just checksum` prints the SwiftPM checksum for the release zip
- `just inspect` records slice, symbol, resource, install-name, and checksum
  metadata

Byte-identical public releases, signing, notarization, dependency digest locks,
and vulnerability response policy are later release-hardening work.

## Smoke Test Representation

The clean-machine dynamic-loading smoke is represented by
`scripts/check-xcframework.sh`: it compiles a temporary C program, `dlopen`s the
framework binary, resolves `ecritum_version` with `dlsym`, checks the version
string, and verifies negative buffer paths.

The persistent C host example is represented in PROJECT.org as M1-006. It will
cover direct include/link behavior with `ecritum.h` outside SwiftPM.

Java runtime unit tests are represented by `native/src/test/java/ecritum/`.
Today they cover the Native Image entrypoint version string and buffer/status
rules.
