# ADR-001: Package Layout and Binary Target Strategy

Status: Accepted

## Context

Ecritum needs to be pleasant for Swift desktop app developers while keeping the
runtime implementation language-neutral. The stable integration surface is a C
ABI wrapped by Swift. The runtime itself is expected to be a GraalVM Native Image
shared library packaged for Apple platforms as an XCFramework.

SwiftPM supports binary targets that point either to a local artifact path or to
a remote archive URL with a checksum. The release package must use a remote
archive so consumers do not need GraalVM, Maven, or Native Image. Contributors
need a local flow that builds the runtime and runs tests before a release archive
exists.

The current repo is still documentation-only. PLAN.org sketches `native/pom.xml`
and `dist/EcritumRuntime.xcframework.zip`, while the current `justfile` still
checks for a root `pom.xml` and `build/native`. This ADR resolves that direction
before M1 scaffolding.

## Decision

Use this package layout:

```text
Package.swift
Sources/
  Ecritum/
  CEcritum/
    include/
      ecritum.h
    shim.c
native/
  pom.xml
  src/main/java/ecritum/...
scripts/
  build-xcframework.sh
  package-artifact.sh
  inspect-artifact.sh
  check-abi.sh
Examples/
  SwiftHost/
  CHost/
Tests/
  EcritumTests/
dist/
  local/
    EcritumRuntime.xcframework
  release/
    EcritumRuntime.xcframework.zip
```

`Ecritum` is the Swift wrapper target and public Swift product. `CEcritum` is a
regular SwiftPM C target with public headers and a minimal `shim.c`, not a
system-library target. The native runtime is a SwiftPM binary target named
`EcritumRuntime`. The exact target graph is:

```text
Product Ecritum
  -> target Ecritum
       -> target CEcritum
       -> binary target EcritumRuntime
```

Consumers depend only on the `Ecritum` product.

`Sources/CEcritum/include/ecritum.h` is the public ABI header. GraalVM generated
headers and isolate details are implementation details and must not become the
public integration surface.

The Maven project lives under `native/pom.xml`. `just build-java`,
`just test-java`, and `just native` must call Maven with `-f native/pom.xml`.
M1 native build outputs are copied into `build/native/macos-arm64/` as the
stable script boundary. Maven's `native/target/` remains an implementation
detail. Multi-platform naming is deferred until M7.

`just xcframework` reads `build/native/**/libecritum.dylib`, wraps it as a
framework, and uses `xcodebuild -create-xcframework` to write
`dist/local/EcritumRuntime.xcframework`. `just package-artifact` zips that
XCFramework into `dist/release/EcritumRuntime.xcframework.zip` with normalized
metadata and emits the SwiftPM checksum. The release zip must contain
`EcritumRuntime.xcframework` at the archive root.

`dist/` and `build/` are generated outputs and must be gitignored once source
scaffolding is added.

M1 local artifacts are not public release artifacts. Full signing,
notarization, reproducibility, SBOM, license notices, and numeric size/perf
budgets are tracked by later ADRs. M1 still proves the artifact shape and
inspection gates.

The M1 artifact tree is:

```text
dist/local/EcritumRuntime.xcframework/
  Info.plist
  macos-arm64/
    EcritumRuntime.framework/
      Info.plist
      EcritumRuntime
      Headers/
        ecritum.h
      Modules/
        module.modulemap
      Resources/
```

The framework binary is the Native Image shared library renamed or installed as
`EcritumRuntime` according to Apple framework conventions. `ecritum.h` is copied
from `Sources/CEcritum/include/ecritum.h` so C and Swift consumers see the same
ABI declarations. No loose runtime files may sit beside the framework. Future
language resources must be embedded in the native image or loaded
bundle-relative from the framework `Resources/` directory.

The framework install name and runtime search paths must be suitable for SwiftPM
and app embedding: use an `LC_ID_DYLIB` compatible with `@rpath` loading, avoid
absolute build-machine paths, and verify with `otool`.

## Manifest Strategy

`Package.swift` is the canonical manifest. It chooses the runtime binary target
by artifact availability:

- If `dist/local/EcritumRuntime.xcframework` exists, the manifest uses
  `.binaryTarget(name: "EcritumRuntime", path: "dist/local/EcritumRuntime.xcframework")`.
- Otherwise, release manifests use
  `.binaryTarget(name: "EcritumRuntime", url: ".../EcritumRuntime.xcframework.zip", checksum: "...")`.

M1 should implement local-artifact detection in the manifest with
`FileManager.default.fileExists(atPath:)`, then prove the toolchain accepts that
manifest behavior. If it does not, use the generated release-manifest fallback
below and record the debt.

The local path artifact and the unzipped release artifact must have identical
structure. Tagged releases must never ship a path-based binary target. Release
validation must fail if the manifest resolves to `.binaryTarget(path:)` while
preparing a public tag.

Before the first public release, the remote URL and checksum are placeholders and
plain `swift test` is not the canonical command. Contributors use
`mise exec -- just test`. Until the local XCFramework exists, `just test` runs
`plan-check` and any other scaffold-independent checks, then prints the missing
`just xcframework` prerequisite instead of invoking a failing SwiftPM build.
After `mise exec -- just xcframework`, `mise exec -- just test-swift` and plain
`swift test` must build and run the Swift tests.

Any checked-in or generated stub artifact must be valid for macOS arm64, export
`ecritum_version`, and be recorded in DEBT.md with an exit condition. Invalid
empty placeholder framework directories are not allowed.

Release packaging must verify the URL/checksum manifest path before tagging a
release. If SwiftPM manifest-time local-artifact detection is not accepted by the
toolchain during M1 implementation, the fallback is a generated release manifest
step owned by `scripts/package-artifact.sh`; that fallback must be recorded in
DEBT.md with an exit condition.

## SwiftPM Details

`Package.swift` should declare:

- `// swift-tools-version: 5.9` or newer.
- `platforms: [.macOS(.v14)]`.
- Product: `.library(name: "Ecritum", targets: ["Ecritum"])`.
- Targets:
  - `Ecritum`, depending on `CEcritum` and `EcritumRuntime`.
  - `CEcritum`, with `publicHeadersPath: "include"`.
  - `EcritumRuntime`, as the local or release binary target.
  - `EcritumTests`.

Phase 0/M1 ships macOS arm64 first. x86_64 and universal release artifacts are
M7 work unless M1 implementation proves them cheaply.

## Script and Task Boundaries

`just` tasks orchestrate. Scripts do the reusable work.

- `just build-java`: runs Maven with `-f native/pom.xml`.
- `just test-java`: runs Java tests with `-f native/pom.xml`.
- `just native`: builds the Native Image shared library and copies the dylib into
  `build/native/<platform>/<arch>/`.
- `just xcframework`: calls `scripts/build-xcframework.sh`.
- `just package-artifact`: calls `scripts/package-artifact.sh`.
- `just checksum`: prints only the SwiftPM checksum for the release zip to
  stdout.
- `just inspect`: calls `scripts/inspect-artifact.sh`.
- `just check-abi`: calls `scripts/check-abi.sh`.
- `just test-swift`: requires `dist/local/EcritumRuntime.xcframework`.
- `just test`: runs the currently available checks and includes Swift tests once
  the package and local artifact exist.

Scripts must be noninteractive, expose `--help`, send diagnostics to stderr, exit
nonzero on failure, accept input/output paths as arguments, and work outside the
repository when all required paths are supplied.

Machine-readable data goes to stdout. Human diagnostics go to stderr.

`just inspect` must print JSON containing at least:

- public symbols
- linked dylibs
- rpaths and install names
- bundled resources
- XCFramework slices and per-slice size
- archive checksum
- code-signing/notarization status when available
- minimum macOS version
- embedded runtime list
- artifact paths

The clean contributor flow is:

```sh
mise trust
mise exec -- just setup
mise exec -- just native
mise exec -- just xcframework
mise exec -- just test
```

Plain `swift test` is also expected to work after `just xcframework`.

## Examples

`Examples/SwiftHost` consumes the Swift product through SwiftPM and calls
`Ecritum.version`.

`Examples/CHost` consumes `ecritum.h` and links the selected framework slice with
explicit compiler/linker flags from a `just` task, for example `-F
dist/local/EcritumRuntime.xcframework/macos-arm64 -framework EcritumRuntime`.
The clean-machine dynamic-loading smoke may also use `dlopen` against the
framework binary and `dlsym("ecritum_version")`. CHost is the stable C ABI smoke
test and must not depend on SwiftPM.

## Alternatives Considered

1. Build GraalVM Native Image from SwiftPM.
   Rejected. SwiftPM package resolution must not require GraalVM, Maven, a JDK,
   or Native Image for consumers.

2. Check in the XCFramework.
   Rejected for now. It makes the source repository heavy and complicates review.
   Release artifacts belong in GitHub releases or equivalent artifact storage.

3. Use a `systemLibrary` target for `CEcritum`.
   Rejected. Ecritum ships its own headers and runtime artifact; it is not
   adapting a separately installed system library.

4. Keep Maven at the repository root.
   Rejected. The Swift package root should stay clean, and native Java/GraalVM
   source belongs under `native/`.

5. Use two hand-maintained package manifests.
   Rejected unless the canonical manifest strategy fails in M1. Two manifests
   will drift and make release validation harder.

## Consequences

- M1 implementation must update the current `justfile` to use `native/pom.xml`
  and the `build/native` to `dist/local` artifact path.
- `swift test` is only a valid direct contributor command after a local
  XCFramework exists. `mise exec -- just test` is the canonical pre-artifact
  command.
- Release packaging must prove both local path and remote URL/checksum binary
  target flows.
- `CEcritum` has a small source file solely to make the C target explicit and
  portable.
- The local/release manifest switch is a release risk and must be covered by
  validation so a public tag never points at a local path.
- GraalVM `@CEntryPoint` constraints are kept behind the stable Ecritum ABI:
  static entry points, simple C-compatible parameters, explicit isolate handling,
  and no uncaught exceptions crossing the C boundary.
- The resource-bundle question for GraalPy and TruffleRuby is deferred to later
  runtime inclusion ADRs.

## Verification Plan

M1 must verify:

- `mise exec -- just test` before source scaffolding continues to run plan checks.
- `mise exec -- just build-java` uses `native/pom.xml`.
- `mise exec -- just native` produces
  `build/native/macos-arm64/libecritum.dylib`.
- `mise exec -- just xcframework` produces
  `dist/local/EcritumRuntime.xcframework`.
- `mise exec -- just test-swift` runs after the local XCFramework exists.
- `swift test` runs after the local XCFramework exists.
- Tests fail with clear diagnostics for missing, stale, wrong-architecture, or
  header/symbol-mismatched artifacts.
- `mise exec -- just package-artifact` produces a SwiftPM-accepted
  `dist/release/EcritumRuntime.xcframework.zip`.
- `mise exec -- just checksum` uses `swift package compute-checksum` and prints
  only the checksum.
- `mise exec -- just inspect` reports symbols, linked dylibs, bundled resources,
  size, checksum, and runtime list.
- `mise exec -- just check-abi` verifies the checked-in public symbol manifest.
- `swift test` calls `Ecritum.version` through `CEcritum` after
  `just xcframework`.
- `Examples/CHost` includes `ecritum.h`, links the XCFramework dylib directly,
  calls `ecritum_version`, and asserts non-empty output.
- A clean-machine smoke test can run `dlopen + ecritum_version` without GraalVM,
  a JDK, or language runtimes installed.
- Clean-machine artifact inspection with `otool -L` shows no external GraalVM or
  JDK paths.

## Deferred To

- Final XCFramework release, signing, and checksum policy ADR.
- License inventory and redistribution policy ADR.
- Vulnerability response, SBOM, CVE tracking, and revocation ADR.
- Artifact reproducibility, signing, and checksum policy ADR.
- Performance and artifact budget policy ADR.

## Reviewers

- Architecture Expert Engineer
- Swift API and Developer Experience Engineer
- Release, Licensing, and Distribution Engineer
- Unix Philosophy and Reusable Components Engineer
- TDD, Testability, and Verification Engineer
- Claude plan consult
