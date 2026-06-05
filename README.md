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

## Initial Language Plan

The first target is not "run every language on the JVM." The first target is a practical, packageable scripting runtime.

- Clojure: first-class through SCI with Babashka-compatible namespaces.
- JavaScript: through GraalJS.
- Lua: through LuaJ or another pure-Java Lua implementation that compiles into Native Image.
- Python: through GraalPy after size and packaging tests.
- Ruby: through TruffleRuby after size, licensing, and packaging tests.
- JVM bytecode/JARs: future research, not an MVP promise.

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
- single binary where possible
- otherwise a small number of bundled dylibs

This is why the current plan favors Native Image shared-library output over embedding a full JVM distribution. SwiftPM consumers should receive a prebuilt binary target; contributors use `mise` and `just` to build the runtime from source.

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

## Design Priorities

- Simple SwiftPM integration.
- Stable C ABI for future Rust, Zig, C++, and other wrappers.
- No C++ ABI in the public interface.
- Explicit host capability model.
- Measured binary size and startup cost.
- Clear license inventory before public release.
- macOS first; other platforms later.

## Open Questions

- Final runtime list for v0.
- Binary size with SCI plus GraalJS.
- Whether GraalPy and TruffleRuby are practical in the same native library.
- Async host callback and executor behavior.
- Full sandbox threat model for user-provided scripts.
- Final license and third-party notice strategy.
