# Ecritum Agent Instructions

## Project Shape

Ecritum is a macOS-first embeddable polyglot scripting runtime for Swift desktop apps. The public integration surface is SwiftPM plus a stable C ABI, so other language wrappers can be added later without exposing a C++ or JVM-specific API.

The planned core is a GraalVM Native Image shared library (`.dylib` or `XCFramework`) that embeds a curated set of runtimes and exposes `ecritum_*` C functions. The app developer should not need to install GraalVM, a JDK, Python, Ruby, Node, or Clojure separately to use the packaged runtime.

## Hard Boundaries

- Use `jj` for version control. Do not use `git` commands.
- Keep the public ABI C-compatible: opaque handles, explicit ownership, explicit error objects, no exceptions crossing FFI.
- Treat Swift as the primary developer experience, but keep the core host API language-neutral.
- Do not claim arbitrary JVM/JAR support until a separate Espresso/full-JVM design is implemented, tested, and licensed.
- Do not bundle a full JDK unless a design document explicitly accepts the size, licensing, and distribution cost.
- Keep dynamic-library distribution as the default plan: single binary where practical, otherwise a small set of bundled dylibs.

## Runtime Plan

- First-class host API: register host functions, callbacks, values, and capability objects from C/Swift.
- First-class Clojure scripting: use SCI or a similar native-image-friendly Clojure interpreter.
- JavaScript support: target GraalJS where native-image embedding is practical.
- Python/Ruby support: treat GraalPy and TruffleRuby as optional/experimental until packaging, licensing, and binary size are measured.
- JVM-language support: Clojure via SCI first; arbitrary Kotlin/Clojure/JAR execution is not an MVP feature.

## Engineering Rules

- Keep host capabilities explicit. Scripts should only access APIs the host registered.
- Design cancellation, timeouts, resource limits, and error reporting before exposing untrusted-user scripting.
- Track binary size, cold start time, and memory overhead as product requirements.
- Maintain a license inventory for GraalVM CE, Native Image/SubstrateVM, Truffle, GraalJS, GraalPy, TruffleRuby, SCI, and transitive dependencies.
- Prefer small examples that prove distribution behavior: a C host, a SwiftPM host, and a packaged macOS app loading the runtime.
- Document every public C symbol before stabilizing it.

## Naming

Ecritum is a coined name inspired by French `ecrit`/`ecriture` and Latin `scriptum`: a written thing, a script, or an inscription. Use the plain ASCII spelling `Ecritum` in package names, symbols, and filenames.
