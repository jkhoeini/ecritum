# 4. Enable a narrow filesystem capability (read-only root)

Goal: let scripts read from one directory — and nothing else — safely.

A new runtime is **deny-by-default**: filesystem, network, process,
environment, clock, random, and log are all denied. If a script never needs the
disk, prefer passing data in via a host function ([Tutorial 2](02-register-host-functions.md))
and grant **no** capability at all. Grant a capability only when the use case
genuinely requires it, and grant the narrowest one that works.

## Grant exactly one read-only root

Build the policy by starting from `.defaultDeny` and narrowing one axis. A
read-only filesystem root is the smallest filesystem grant:

```swift
import Ecritum
import Foundation

// The single directory scripts may read from. Use a real, absolute file URL.
let scriptsRoot = URL(fileURLWithPath: "/Users/me/Library/Application Support/MyApp/scripts",
                      isDirectory: true)

let runtime = try EcritumRuntime(.init(
    languages: [.clojure],
    // Everything else stays denied; only this one read-only root is added.
    policy: .defaultDeny.withFilesystem(.readOnly(roots: [try .directory(scriptsRoot)]))
))
let context = try runtime.context()
defer { try? context.close(); try? runtime.close() }
```

`EcritumPermissionPolicy.FilesystemRoot.directory(_:)` validates the URL: it must
be a `file://` URL with an absolute, normalized path (no `.`/`..` components, no
trailing slash except `/`). An invalid root throws `EcritumError.invalidConfig`
before the runtime starts. `roots` must be non-empty and unique.

## Read inside the root from a script

The filesystem facade is `ecritum.fs`. In Clojure it is `ecritum.fs/read-text`
(kebab-case); in JavaScript, Lua, Python, and Ruby it is
`ecritum.fs.readText(...)` (camelCase).

```swift
let inside = scriptsRoot.appendingPathComponent("greeting.txt")

let text = try await context.eval(EcritumScript(
    "(ecritum.fs/read-text \"\(inside.path)\")",
    language: .clojure,
    sourceName: "read-inside.clj"
))
// text == .string(<contents of greeting.txt>)
```

`read-text` returns a `.string`; `read-bytes` (`readBytes`) returns `.data`.

## Anything outside the root is denied

A read of any path **outside** the granted root fails with
`EcritumError.permissionDenied` (status `.permissionDenied`, category
`.permission`):

```swift
do {
    _ = try await context.eval(EcritumScript(
        "(ecritum.fs/read-text \"/etc/hosts\")",
        language: .clojure,
        sourceName: "read-outside.clj"
    ))
} catch let error as EcritumError {
    assert(error.status == .permissionDenied)
    assert(error.category == .permission)
    // error.details?.sourceName == "read-outside.clj"
}
```

This in-root-succeeds / out-of-root-denied behavior is exactly what the
`testArtifactBackedClojureFilesystemFacadeUsesConfiguredRoot` test verifies
([`Tests/EcritumTests/EcritumEvalTests.swift`](../../Tests/EcritumTests/EcritumEvalTests.swift)).
See [Tutorial 6](06-interpret-errors-and-denials.md) for reading the error.

## Security implications

- **Default-deny is the floor.** Until you call `.withFilesystem(...)` the disk
  is invisible to scripts. The same is true for `.withNetwork`, `.withProcess`,
  `.withEnvironment`, `.withClock`, `.withRandom`, and `.withLog`.
- **Read-only means read-only.** Use `.readOnly(roots:)` unless writes are
  truly required; `.readWrite(roots:)` exists but grants more. There is no
  "allow all paths" mode — you always name explicit roots.
- **Roots are exact directory grants.** Reads resolve against the listed roots;
  paths outside them are denied, not silently widened.
- **Narrow further per context.** `EcritumContext.Configuration` accepts a
  `policy` *narrowing* so a specific context can be more restrictive than the
  runtime (e.g. drop the filesystem back to denied for an untrusted script).
  A narrowing can only tighten, never widen, what the runtime granted.
- **Still no package access.** Granting a filesystem root does not enable
  `pip`, RubyGems, Bundler, native extensions, or package downloads — those
  stay denied regardless of policy (see [Tutorial 6](06-interpret-errors-and-denials.md)).

## Verify the capability path

The end-to-end read-only behavior is exercised by:

```sh
mise exec -- swift test --filter testArtifactBackedClojureFilesystemFacadeUsesConfiguredRoot
```

Verified result: the test passes — in-root `read-text`/`read-bytes` succeed and
an out-of-root read raises `PERMISSION_DENIED`.

Next: [package a macOS .app](05-package-a-macos-app.md).
