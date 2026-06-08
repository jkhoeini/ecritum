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

## Next Release Language Target

The next target is not "run every language on the JVM." The target is a
practical, packageable scripting runtime in one default Ecritum artifact.
Support is claimed only after strict conformance and strict abuse gates pass with
zero required pending cases for the language.

- Clojure: first-class through SCI with Babashka-compatible namespaces.
- JavaScript: through GraalJS.
- Lua: through LuaJ or another pure-Java Lua implementation that compiles into
  Native Image.
- Python: through GraalPy after Native Image resource packaging, strict
  conformance, strict abuse, license/SBOM, size, startup, and RSS gates pass.
- Ruby: through TruffleRuby after Maven/version feasibility, Native Image
  resource packaging, strict conformance, strict abuse, license/SBOM, size,
  startup, and RSS gates pass.
- JVM bytecode/JARs: future research, not an MVP promise.

Python and Ruby are runtime-and-standard-library/resource-only in the next
release. Ecritum does not support `pip`, RubyGems, Bundler, third-party package
installs, package downloads, native wheels, native gems, C extensions, `ctypes`,
`cffi`, FFI/NFI, mutable package caches, subprocess, raw network, unrestricted
filesystem, environment access, direct Java access, or raw Polyglot access.

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

## Current Reference Metrics

The next final release must publish measured zip size, unzipped framework size,
app bundle delta, cold start, first eval per language, and idle RSS in this
README. The latest measured v0.2.0-alpha.1 default artifact reference from the
current release gate is:

| Metric | Value |
| --- | ---: |
| Hosted zip size | 58,541,515 bytes |
| Unzipped XCFramework size | 151,368,769 bytes |
| Framework bundle size | 147,864 KiB |
| Packaged smoke app size | 148,504 KiB |
| C host cold start | p50 18.618 ms, p95 195.009 ms |
| First eval | p50 1.356 ms, p95 2.219 ms |
| Idle RSS after first call | p50 15,810,560 bytes, p95 15,810,560 bytes |
| SwiftPM checksum | `edfe358e9e98a5133080e147a4069b42a9a8c20a5b1b917464113da61b17358e` |
| Included runtimes | Clojure, JavaScript, Lua |

The packaged app size is not an app bundle delta. M13 tracks measuring the delta
against a comparable app without Ecritum.

## Design Priorities

- Simple SwiftPM integration.
- Stable C ABI for future Rust, Zig, C++, and other wrappers.
- No C++ ABI in the public interface.
- Explicit host capability model.
- Measured binary size and startup cost.
- MIT first-party license with clear third-party runtime inventory.
- macOS first; other platforms later.

## Open Questions

- GraalPy resource packaging and sandbox feasibility inside the default artifact.
- TruffleRuby Maven/version feasibility and resource packaging inside the default
  artifact.
- Async host callback and executor behavior.
- Complete five-language sandbox evidence for user-provided scripts.
- Optional Trusted macOS release operations with Developer ID signing and
  notarization.
