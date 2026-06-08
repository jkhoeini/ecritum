# Ecritum Tutorials

Task-oriented how-tos for using Ecritum from a Swift app. Each page is short,
shows copy-pasteable code/commands that match the **current** Swift API and
`just` recipes, and points to a runnable example under [`Examples/`](../../Examples).

These tutorials assume macOS arm64, [`mise`](https://mise.jdx.dev) for tool
versions, and the prebuilt `EcritumRuntime.xcframework`. Contributors build the
artifact once with `mise exec -- just xcframework`; SwiftPM consumers resolve a
hosted release artifact (see Tutorial 1).

| # | Tutorial | You will learn to |
| --- | --- | --- |
| 1 | [Add Ecritum to a Swift app and run a first eval](01-add-to-a-swift-app.md) | Wire the SwiftPM dependency and evaluate your first script. |
| 2 | [Register host functions and call them from a script](02-register-host-functions.md) | Expose host data/behavior and call it from guest code. |
| 3 | [Write and evaluate scripts in each language](03-scripts-in-each-language.md) | Run Clojure, JavaScript, Lua, Python, and Ruby snippets. |
| 4 | [Enable a narrow filesystem capability](04-narrow-filesystem-capability.md) | Grant a read-only root safely under deny-by-default. |
| 5 | [Package a macOS .app that embeds the runtime](05-package-a-macos-app.md) | Ship `EcritumRuntime.framework` inside a `.app` bundle. |
| 6 | [Interpret errors and default-deny failures](06-interpret-errors-and-denials.md) | Read status/category/message, handle `PERMISSION_DENIED`, understand package limits. |

## Core mental model

- **One artifact, five languages.** The default `EcritumRuntime.xcframework`
  ships Clojure, JavaScript, Lua, Python, and Ruby, all sandboxed. There is no
  separate Core/Full choice and no separate Ruby artifact.
- **Deny by default.** A new runtime grants no filesystem, network, process,
  environment, clock, random, or log capability. Scripts only see what the host
  explicitly registers (host functions) or enables (a versioned policy).
- **The host owns the trust boundary.** Data crosses the boundary as
  `EcritumValue`. Host functions are the normal way to feed data in and read
  results out; capabilities are for cases that genuinely need filesystem,
  network, etc.
- **Python and Ruby are runtime-and-standard-library only.** There is no `pip`,
  RubyGems, Bundler, third-party package install, native wheel/gem, or C/native
  extension support. See Tutorial 6 for the user-facing detail.

## Conventions used in these tutorials

- Commands run from the repository root unless noted.
- `mise exec -- <tool>` runs a pinned tool (`just`, `swift`).
- Examples run against the local artifact via `DYLD_FRAMEWORK_PATH` pointing at
  the matching `macos-<arch>` slice; a packaged `.app` (Tutorial 5) needs no
  such override.

See also the [README](../../README.md) for the support matrix and reference
metrics, and [`docs/release-gates.md`](../release-gates.md) for the gate
baselines that back the "supported" claim.
