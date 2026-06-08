# 6. Interpret errors and default-deny failures

Goal: read Ecritum errors, handle denied operations, and understand the
Python/Ruby package limits in user-facing terms.

## The error shape

Ecritum operations throw `EcritumError`. Every case (except
`.runtimeArtifactMissing`) carries an `EcritumErrorDetails`. The fields you
program against are:

- `error.status` — a stable `EcritumStatus` (e.g. `.permissionDenied`,
  `.script`, `.timeout`, `.invalidConfig`, `.closed`). Optional, since
  `.runtimeArtifactMissing` has no status.
- `error.category` — a stable, machine-readable `EcritumErrorCategory` string
  (e.g. `permission_denied`, `permission`, `script`, `invalid_config`). Use this
  for logging/branching that should survive message wording changes.
- `error.details?.message` — a safe, human-readable summary.
- `error.details?.operation`, `.language`, `.sourceName` — context for which
  call, language, and script failed.

```swift
do {
    let result = try await context.eval(script)
    use(result)
} catch let error as EcritumError {
    switch error.status {
    case .permissionDenied:
        // A capability the script needs was not granted.
        print("denied:", error.details?.message ?? "")
    case .script:
        // The guest code itself failed (syntax/runtime error in the script).
        print("script error in", error.details?.sourceName ?? "?", "-", error.details?.message ?? "")
    case .timeout:
        print("script exceeded its time budget")
    default:
        print("ecritum error:", error.category.rawValue, "-", error.details?.message ?? "")
    }
}
```

## PERMISSION_DENIED on denied operations

Because capabilities are deny-by-default, a script that attempts a denied
operation does not crash the host — it surfaces as
`EcritumError.permissionDenied` with status `.permissionDenied` and category
`.permission`. The denied source name is preserved in `details?.sourceName`.

```swift
// Runtime created with .defaultDeny — the clock capability is denied.
do {
    _ = try await context.eval(EcritumScript(
        "(ecritum.time/now)",
        language: .clojure,
        sourceName: "needs-clock.clj"
    ))
} catch let error as EcritumError {
    assert(error.status == .permissionDenied) // category == .permission
}
```

The fix is to grant the narrowest capability that the operation needs (for the
filesystem, see [Tutorial 4](04-narrow-filesystem-capability.md); the same
`withNetwork` / `withClock` / `withRandom` / `withProcess` / `withEnvironment` /
`withLog` builders cover the other axes). This `PERMISSION_DENIED`-on-denied
behavior is verified by the policy tests in
[`Tests/EcritumTests/EcritumEvalTests.swift`](../../Tests/EcritumTests/EcritumEvalTests.swift)
(see the time, http, and filesystem denial cases).

## Redaction (don't expect raw internals)

By default, diagnostics are **redacted** (`EcritumDiagnosticsPolicy.redacted`).
The Swift wrapper drops messages and fields that look like host internals or
sensitive data — JVM/Graal/Truffle class names, stack-trace markers, absolute
user/temp paths, and `secret`/`token=`/`password=` markers are replaced or
stripped. Empty or unsafe values fall back to a generic
`"Ecritum operation failed"`. So:

- Treat `status`/`category` as the reliable, stable signal.
- Treat `message` as a safe summary, not a debugging dump.
- Line/column and stack frames are reserved for a future ABI and are not
  populated by current native-backed errors.

`EcritumDiagnosticsPolicy.raw` exists for trusted local debugging, but redacted
is the default and the right choice for anything that ships.

## Python and Ruby package limitations (user-facing)

Python (GraalPy) and Ruby (TruffleRuby, pure-Ruby sandboxed mode) ship as
**runtime and standard library only**. State this plainly to your users:

- There is **no package installation**: no `pip`, no `pip install`, no
  RubyGems, no `gem install`, no Bundler, and no package downloads or mutable
  package caches.
- There are **no third-party packages, native wheels, or native gems**, and no
  C/native extensions (`ctypes`, `cffi`, `fiddle`, FFI/NFI are not available).
  Ruby runs without its LLVM/Sulong backend, so any path needing native C
  extensions stays unavailable.
- Scripts use the bundled standard library only. In Python the sandbox seals
  `__import__`, so `import` statements are rejected — write builtin-only Python
  (see [`Examples/PythonTextProcessing`](../../Examples/PythonTextProcessing)).

This is a deliberate sandbox boundary, **not** a bug and **not** a sign that the
language is unsupported: Clojure, JavaScript, Lua, Python, and Ruby are all
supported in the default artifact. If a user asks why their `pip`/gem-based code
does not run, the answer is "Ecritum runs these languages with the standard
library only; third-party packages and native extensions are out of scope," not
"the language is blocked." The same default-deny rules (no raw network,
subprocess, unrestricted filesystem, environment, or direct Java/Polyglot
access) apply to every language, including Python and Ruby.

## Quick reference

| You see | Likely meaning | Action |
| --- | --- | --- |
| `.permissionDenied` / `permission` | Script used a denied capability | Grant the narrowest capability, or pass data via a host function |
| `.script` | Guest code has a syntax/runtime error | Fix the script; read `details?.sourceName` |
| `.timeout` | Script exceeded its time budget | Optimize the script or raise the limit |
| `.invalidConfig` | Bad policy/config (e.g. invalid root) | Fix the configuration before runtime creation |
| `.closed` | Used a runtime/context/namespace after `close()` | Recreate it; respect lifecycle order |
| `.runtimeArtifactMissing` | No artifact resolved by Package.swift | Build/resolve the artifact (Tutorial 1) |
| `import` rejected in Python | Sandbox seals `__import__` | Use builtins only |

Back to the [tutorials index](README.md).
