# Ecritum

Ecritum is a planned macOS-first polyglot scripting library for Swift apps. It gives desktop app developers a C/Swift API for exposing host functions to embedded user scripts, without asking end users to install a JDK or language runtimes.

## What It Is

Ecritum is intended to be distributed as a native dynamic library, likely produced with GraalVM Native Image. The library exposes a stable C ABI, and the Swift package wraps that ABI with a Swift-native API.

At runtime, a host app registers capabilities:

- functions
- callbacks
- values
- configuration objects
- app-specific services

User scripts then call those registered capabilities from supported scripting languages.

## Initial Language Plan

The first target is not "run every language on the JVM." The first target is a practical, packageable scripting runtime.

- Clojure: first-class through SCI or a similar native-image-friendly interpreter.
- JavaScript: through GraalJS if native-image packaging stays practical.
- Python: optional/experimental through GraalPy after size and packaging tests.
- Ruby: optional/experimental through TruffleRuby after size and packaging tests.
- JVM bytecode/JARs: future research, not an MVP promise.

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

This is why the current plan favors Native Image shared-library output over embedding a full JVM distribution.

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
SCI / GraalJS / optional Truffle languages
```

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
- Best Swift API shape for registering host functions.
- Sandboxing model for user-provided scripts.
- Final license and third-party notice strategy.
