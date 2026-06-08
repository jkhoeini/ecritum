# Ecritum

Ecritum is a planned macOS-first polyglot scripting library for Swift apps. It gives desktop app developers a SwiftPM package for exposing host functions to embedded user scripts, without asking end users to install a JDK or language runtimes.

## What It Is

Ecritum is intended to be distributed as a prebuilt native dynamic library inside an XCFramework, likely produced with GraalVM Native Image. The library exposes a stable C ABI, and the Swift package wraps that ABI with a Swift-native API.

At runtime, a host app registers capabilities:

- functions
- callbacks
- values
- configuration objects
- app-specific services

User scripts then call those registered capabilities from supported scripting languages.

## Planned Swift API

```swift
import Ecritum

let runtime = try EcritumRuntime(
    .init(
        languages: [.clojure],
        policy: .defaultDeny.withFilesystem(
            .readOnly(roots: [try .directory(appScriptsURL)])
        )
    )
)

let app = try runtime.namespace(.init("app"))
try app.register(.init("notify")) { call in
    let message = try call.string(at: 0)
    notifier.enqueue(message)
    return .null
}

let value = try await runtime.context().eval(
    EcritumScript(
        source: "(app/notify \"hello\")",
        language: .clojure,
        sourceName: "plugin.clj"
    )
)
```

The public API should be capability-based and deny-by-default: scripts only see
functions, objects, and standard-library services that the host explicitly
registers and enables through a versioned policy object.

## Supported Languages

The target is not "run every language on the JVM." The target is a practical,
packageable scripting runtime in one default Ecritum artifact. Support is
claimed only after strict conformance and strict abuse gates pass with zero
required pending cases for the language.

The single default `EcritumRuntime.xcframework` ships all five languages below as
supported. There is no separate Core/Full choice and no separate Ruby artifact.

| Language | Engine | Status |
| --- | --- | --- |
| Clojure | SCI (Babashka-compatible namespaces) | Supported in the default artifact |
| JavaScript | GraalJS | Supported in the default artifact |
| Lua | LuaJ (pure-Java, compiled into Native Image) | Supported in the default artifact |
| Python | GraalPy | Supported in the default artifact |
| Ruby | TruffleRuby (pure-Ruby sandboxed mode; LLVM/Sulong backend excluded) | Supported in the default artifact |

JVM bytecode/JARs remain future research, not an MVP promise.

### Unsupported Scope

Python and Ruby ship as runtime-and-standard-library only. Ecritum does not
support `pip`, RubyGems, Bundler, third-party package installs, package
downloads, native wheels, native gems, C/native extensions, `ctypes`, `cffi`,
`fiddle`, FFI/NFI, mutable package caches, subprocess, raw network, unrestricted
filesystem, environment access, direct Java access, or raw Polyglot access.
These are denied by default for every language, including Ruby and Python.

Ruby runs in pure-Ruby sandboxed mode; TruffleRuby's LLVM/Sulong backend is
excluded from the shipped artifact (see
[ADR-0028](docs/adr/0028-ruby-size-budget-and-llvm-exclusion.md)), so any path
that would require native C extensions, FFI, or `fiddle` stays denied.

## Standard Library Plan

Ecritum should expose useful JDK-backed scripting APIs without exposing the whole JDK dynamically.

Planned modules:

- `ecritum.fs`
- `ecritum.io`
- `ecritum.json`
- `ecritum.http`
- `ecritum.time`
- `ecritum.regex`
- `ecritum.crypto`
- `ecritum.env`
- `ecritum.process`
- `ecritum.log`

Filesystem, network, process, and environment access are denied by default and must be enabled by the host app.

## Distribution Model

The desired developer experience is:

```swift
// SwiftPM dependency, then:
import Ecritum
```

The desired user experience is that the app ships with everything it needs:

- no separate GraalVM install
- no separate JDK install
- no separate Python/Ruby/Node/Clojure install
- one `.app` bundle containing the app executable plus
  `Contents/Frameworks/EcritumRuntime.framework`
- language runtimes and resources bundled inside that framework

This is why the current plan favors Native Image shared-library output over embedding a full JVM distribution. SwiftPM consumers should receive a prebuilt binary target; contributors use `mise` and `just` to build the runtime from source.

Normal SwiftPM consumers do not set Ecritum environment variables. SwiftPM should
resolve the checked-in GitHub Release URL/checksum for
`EcritumRuntime.xcframework.zip`; contributors may use `dist/local`. Current
v0.2.0-alpha.1 prerelease validation uses that checked-in URL/checksum path for
normal consumers, while release staging can still override the URL/checksum with
paired environment variables.

## What It Is Not

Ecritum is not planned as a full embedded JDK. It should not initially promise arbitrary JAR execution, arbitrary Kotlin execution, or full Node.js compatibility.

Those are possible research tracks, but they change the product shape: larger bundles, more licensing work, more compatibility testing, and a weaker SwiftPM "it just works" story.

## Core Architecture

```text
Swift app
  |
  | SwiftPM wrapper
  v
Ecritum C ABI
  |
  | opaque handles, callbacks, registered host capabilities
  v
Native Image shared library
  |
  | curated embedded runtimes
  v
SCI / GraalJS / GraalPy / TruffleRuby / LuaJ
```

## Development

Install tools:

```bash
mise trust
mise install
```

List tasks:

```bash
mise exec -- just
```

Run available checks:

```bash
mise exec -- just test
```

Release gate baselines are documented in
[docs/release-gates.md](docs/release-gates.md). See [PLAN.org](PLAN.org) and
[PROJECT.org](PROJECT.org) for the implementation sequence.

## Tutorials

Task-oriented how-tos for using Ecritum from a Swift app live in
[docs/tutorials/](docs/tutorials/README.md):

1. [Add Ecritum to a Swift app and run a first eval](docs/tutorials/01-add-to-a-swift-app.md)
2. [Register host functions and call them from a script](docs/tutorials/02-register-host-functions.md)
3. [Write and evaluate scripts in each language](docs/tutorials/03-scripts-in-each-language.md)
4. [Enable a narrow filesystem capability](docs/tutorials/04-narrow-filesystem-capability.md)
5. [Package a macOS .app that embeds the runtime](docs/tutorials/05-package-a-macos-app.md)
6. [Interpret errors and default-deny failures](docs/tutorials/06-interpret-errors-and-denials.md)

Each tutorial points to a runnable example under [Examples/](Examples).

## Current Reference Metrics

All values below are measured on macOS arm64 for the current five-language
default artifact (`dist/local/EcritumRuntime.xcframework`, runtimes: Clojure,
JavaScript, Lua, Python, Ruby). Reproduce with `mise exec -- just size`, the
`bench-*` recipes, and `package-artifact`/`inspect`.

### Artifact size

| Metric | Value | Source |
| --- | ---: | --- |
| Unzipped XCFramework artifact | 476,886,393 bytes (~476.9 MB) | `just size` |
| Private runtime (`libecritum_graal.dylib`) | 476,230,752 bytes | `just size` |
| Public wrapper binary | 164,160 bytes | `just size` |
| Hosted release zip | 172,157,157 bytes | `package-artifact` |
| SwiftPM package checksum | `2f8170d74abe0f95aafb3e247489c814e0b6165be475790f014f7fcb6f94f146` | `package-artifact-verify` |

The 800 MB default-artifact size budget and the TruffleRuby LLVM/Sulong
exclusion are recorded in
[ADR-0028](docs/adr/0028-ruby-size-budget-and-llvm-exclusion.md).

### Startup and memory

| Metric | Value | Source |
| --- | ---: | --- |
| C host cold start (process elapsed) | p50 23.272 ms, p95 113.579 ms | `just bench-cold-start` |
| `dlopen`+`dlsym` | p50 9.545 ms, p95 13.018 ms | `just bench-cold-start` |
| First wrapper call | p50 2.536 ms, p95 3.156 ms | `just bench-cold-start` |
| Swift host cold start (process elapsed) | p50 28.198 ms, p95 96.312 ms | `just bench-swift-cold-start` |
| Idle RSS after first call | p50 21,741,568 bytes, p95 21,741,568 bytes | `just bench-idle-rss` |
| Python RSS after eval | p50 229,146,624 bytes, p95 242,532,352 bytes | `just bench-python-rss` |
| Ruby RSS after eval | p50 198,000,640 bytes, p95 202,997,760 bytes | `just bench-ruby-rss` |

### First eval per language

| Language | First eval | Source |
| --- | ---: | --- |
| Lua | p50 1.225 ms, p95 1.595 ms | `just bench-lua-first-eval` |
| Clojure (default `bench-first-eval`) | p50 1.732 ms, p95 2.647 ms | `just bench-first-eval` |
| JavaScript | p50 8.083 ms, p95 11.282 ms | `just bench-javascript-first-eval` |
| Python | p50 46.759 ms, p95 53.341 ms | `just bench-python-first-eval` |
| Ruby | p50 62.010 ms, p95 115.695 ms | `just bench-ruby-first-eval` |

App bundle delta (the size a `.app` grows by when it embeds Ecritum versus a
comparable app without it) is not yet measured by a release recipe. The hosted
zip and unzipped artifact sizes above are the authoritative size references.

## Design Priorities

- Simple SwiftPM integration.
- Stable C ABI for future Rust, Zig, C++, and other wrappers.
- No C++ ABI in the public interface.
- Explicit host capability model.
- Measured binary size and startup cost.
- MIT first-party license with clear third-party runtime inventory.
- macOS first; other platforms later.

## Open Questions

- Async host callback and executor behavior.
- Complete five-language sandbox evidence for user-provided scripts.
- App bundle delta measurement against a comparable app without Ecritum.
- Optional Trusted macOS release operations with Developer ID signing and
  notarization.
