# 1. Add Ecritum to a Swift app and run a first eval

Goal: add Ecritum as a SwiftPM dependency and evaluate one script.

Ecritum is a binary-backed SwiftPM package: the Swift `Ecritum` library wraps a
prebuilt `EcritumRuntime.xcframework`. You do not install a JDK, GraalVM, or any
language runtime separately — the runtimes ship inside the artifact.

## Option A — local artifact (developing against this repo)

The examples in this repo depend on Ecritum by **path** and resolve the local
artifact built by `just`. Use this when you are working inside the repository.

1. Build the runtime artifact once (skip if `dist/local/EcritumRuntime.xcframework`
   already exists):

   ```sh
   mise exec -- just xcframework
   ```

2. Point your example/app `Package.swift` at the package path:

   ```swift
   // swift-tools-version: 5.9
   import PackageDescription

   let package = Package(
       name: "MyHost",
       platforms: [.macOS(.v14)],
       dependencies: [
           .package(path: "../.."), // path to this repo's root
       ],
       targets: [
           .executableTarget(
               name: "MyHost",
               dependencies: [
                   .product(name: "Ecritum", package: "Ecritum"),
               ]
           ),
       ]
   )
   ```

   (This is exactly what `Examples/SwiftHost/Package.swift` does.)

## Option B — hosted release artifact (consuming a published version)

For an app outside this repo, depend on a tagged release. SwiftPM resolves the
checked-in release URL/checksum and downloads the binary `EcritumRuntime`
target automatically — no environment variables required for normal consumers.

```swift
// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "MyHost",
    platforms: [.macOS(.v14)],
    dependencies: [
        .package(url: "https://github.com/jkhoeini/ecritum.git", from: "0.2.0-alpha.1"),
    ],
    targets: [
        .executableTarget(
            name: "MyHost",
            dependencies: [
                .product(name: "Ecritum", package: "ecritum"),
            ]
        ),
    ]
)
```

The platform floor is **macOS 14**. Ecritum is macOS-first; other platforms are
future work.

## Write a first eval

`EcritumRuntime` owns the loaded runtime, `EcritumContext` runs scripts, and
`EcritumScript` carries the source, language, and a source name used in
diagnostics. `eval` is `async` and returns an `EcritumValue`.

```swift
import Ecritum
import Foundation

// Guard so a missing artifact fails clearly rather than crashing.
guard Ecritum.runtimeArtifactAvailable else {
    fatalError("Ecritum runtime artifact is not available")
}

// Enable just the language you need. New runtimes are deny-by-default:
// no filesystem, network, process, environment, clock, random, or log.
let runtime = try EcritumRuntime(.init(languages: [.clojure]))
let context = try runtime.context()
defer {
    try? context.close()
    try? runtime.close()
}

let result = try await context.eval(EcritumScript(
    "(+ 40 2)",
    language: .clojure,
    sourceName: "first.clj"
))

print(result.intValue ?? -1) // 42
```

Key API notes:

- `EcritumRuntime.Configuration` takes `languages: Set<EcritumLanguage>`. It is
  empty by default; enable only the languages you actually use.
- `EcritumScript(_:language:sourceName:)` — the source string is the first,
  unlabeled argument.
- `eval` is `async throws`; call it with `try await` from an async context.
- `EcritumValue` is an enum (`.null`, `.bool`, `.int`, `.double`, `.string`,
  `.data`, `.array`, `.object`) with accessors like `.intValue`, `.stringValue`,
  `.arrayValue`, `.objectValue`.

## Run it

The repository ships a minimal first-eval host, `Examples/SwiftHost`, which
prints the loaded runtime version. Run it with:

```sh
mise exec -- just example-swift
```

Verified output:

```
SwiftHost version=0.1.0
```

To run your own executable directly against the local artifact, set
`DYLD_FRAMEWORK_PATH` to the matching slice:

```sh
cd Examples/MyHost
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/MyHost
```

A packaged `.app` does **not** need `DYLD_FRAMEWORK_PATH` — see
[Tutorial 5](05-package-a-macos-app.md).

Next: [register host functions](02-register-host-functions.md).
