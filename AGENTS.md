# Ecritum Agent Instructions

## Project Shape

Ecritum is a macOS-first embeddable polyglot scripting runtime for Swift desktop apps. The public integration surface is SwiftPM plus a stable C ABI, so other language wrappers can be added later without exposing a C++ or JVM-specific API.

The planned core is a GraalVM Native Image shared library (`.dylib` or `XCFramework`) that embeds a curated set of runtimes and exposes `ecritum_*` C functions. The app developer should not need to install GraalVM, a JDK, Python, Ruby, Node, or Clojure separately to use the packaged runtime.

## Hard Boundaries

- Use `jj` for version control. Do not use `git` commands.
- Use `mise` for project tools and `just` for project tasks. Prefer `mise exec -- just <task>` if tool versions matter.
- Keep the public ABI C-compatible: opaque handles, explicit ownership, explicit error objects, no exceptions crossing FFI.
- Treat Swift as the primary developer experience, but keep the core host API language-neutral.
- Do not claim arbitrary JVM/JAR support until a separate Espresso/full-JVM design is implemented, tested, and licensed.
- Do not bundle a full JDK unless a design document explicitly accepts the size, licensing, and distribution cost.
- Keep dynamic-library distribution as the default plan: single binary where practical, otherwise a small set of bundled dylibs.
- Do not rely on SwiftPM running GraalVM builds for consumers. Consumer packages must use prebuilt binary artifacts.

## Runtime Plan

- First-class host API: register host functions, callbacks, values, and capability objects from C/Swift.
- First-class Clojure scripting: use SCI with Babashka-compatible namespaces and behavior.
- JavaScript support: target GraalJS.
- Python support: target GraalPy, with size and resource packaging gates.
- Ruby support: target TruffleRuby, with size, licensing, and resource packaging gates.
- Lua support: start with LuaJ or another pure-Java implementation that works in Native Image; do not assume an official Truffle Lua runtime exists.
- JVM-language support: Clojure via SCI first; arbitrary Kotlin/Clojure/JAR execution is not an MVP feature.

## Engineering Rules

- Keep host capabilities explicit. Scripts should only access APIs the host registered.
- Design cancellation, timeouts, resource limits, and error reporting before exposing untrusted-user scripting.
- Track binary size, cold start time, and memory overhead as product requirements.
- Maintain a license inventory for GraalVM CE, Native Image/SubstrateVM, Truffle, GraalJS, GraalPy, TruffleRuby, SCI, and transitive dependencies.
- Prefer small examples that prove distribution behavior: a C host, a SwiftPM host, and a packaged macOS app loading the runtime.
- Document every public C symbol before stabilizing it.
- Expose useful JDK capabilities through a curated Ecritum standard library. Do not enable unrestricted Java class lookup, reflection, class loading, native library loading, process execution, filesystem, network, or environment access by default.

## Task Commands

- `just setup`: install project tools with mise.
- `just doctor`: print tool versions.
- `just test`: run currently available checks.
- `just native`: build the Native Image shared library once source scaffolding exists.
- `just xcframework`: assemble the SwiftPM binary artifact once native artifacts exist.
- `just license-report`: generate third-party notices once dependencies exist.

Use `mise trust` once before running project commands on a fresh checkout.

## Naming

Ecritum is a coined name inspired by French `ecrit`/`ecriture` and Latin `scriptum`: a written thing, a script, or an inscription. Use the plain ASCII spelling `Ecritum` in package names, symbols, and filenames.
