# 5. Package a macOS .app that embeds the runtime

Goal: produce a `.app` bundle that runs Ecritum scripts with no
`DYLD_*` overrides and no separately installed runtimes.

The shipping shape is one `.app` whose `Contents/Frameworks/` holds
`EcritumRuntime.framework`, with the language runtimes and resources bundled
inside that framework. The end user installs nothing else — no JDK, no GraalVM,
no Python/Ruby/Node/Clojure.

## The reference: MacSmokeApp + packaged-app-smoke

[`Examples/MacSmokeApp`](../../Examples/MacSmokeApp) is a minimal `@main`
executable that creates a runtime over all five languages, registers a host
function, and evaluates one script per language. The
[`scripts/test-packaged-app-smoke.sh`](../../scripts/test-packaged-app-smoke.sh)
script builds that executable, assembles a `.app`, copies the packaged
`EcritumRuntime.framework` into `Contents/Frameworks/`, and runs the bundle from
a temp location with **no** `DYLD_FRAMEWORK_PATH`.

Run the whole flow:

```sh
mise exec -- just packaged-app-smoke
```

On success the packaged app prints exactly (verified):

```
EcritumSmokeApp version=0.1.0 clojure=42 javascript=42 lua=42 python=42 ruby=42
```

The script also asserts, as part of the smoke gate, that the bundle:

- runs with no `DYLD_*` runtime override,
- has no `LC_DYLD_ENVIRONMENT` load command,
- does not embed any workspace-local install path, and
- does not link a build-machine runtime path (no `/GraalVM`, `/jdk`,
  `/native/target`, `/build/native`).

## What the bundle layout looks like

```
EcritumSmoke.app/
  Contents/
    MacOS/
      EcritumSmokeApp                       # your executable
    Frameworks/
      EcritumRuntime.framework/
        EcritumRuntime                      # public wrapper binary
        Resources/
          libecritum_graal.dylib            # private runtime + bundled languages
```

The executable links `EcritumRuntime.framework` via `@rpath`, and the framework
carries the private `libecritum_graal.dylib` (the Native Image shared library
that contains the five language runtimes).

## Doing this in your own app

1. Build the artifact and your executable against it (see
   [Tutorial 1](01-add-to-a-swift-app.md)).
2. Copy the matching slice's framework into the bundle:

   ```sh
   slice="macos-$(uname -m)"
   cp -R "dist/local/EcritumRuntime.xcframework/$slice/EcritumRuntime.framework" \
     "MyApp.app/Contents/Frameworks/EcritumRuntime.framework"
   ```

3. Ensure the executable's runtime search path includes
   `@executable_path/../Frameworks` (Xcode's "Embed Frameworks" build phase does
   this for you; for a hand-built bundle, link with
   `-Wl,-rpath,@executable_path/../Frameworks`).
4. Run the bundle with **no** `DYLD_FRAMEWORK_PATH` and confirm it loads.

`test-packaged-app-smoke.sh` is the executable spec for these steps — read it if
you need the exact framework copy, code-loading, and rpath wiring.

## Notes and current scope

- **Size.** The framework is large because it bundles five language runtimes.
  See the README's reference metrics (unzipped artifact ~476.9 MB; hosted zip
  ~172.2 MB) and [ADR-0028](../adr/0028-ruby-size-budget-and-llvm-exclusion.md)
  for the size budget and the TruffleRuby LLVM/Sulong exclusion.
- **Signing/notarization.** Developer ID signing and notarization for trusted
  release operations are tracked as open work, not part of this smoke flow.
- **App bundle delta.** The size a `.app` grows by versus a comparable app
  without Ecritum is not yet measured by a release recipe; use the artifact/zip
  sizes above as the authoritative references.

Next: [interpret errors and default-deny failures](06-interpret-errors-and-denials.md).
