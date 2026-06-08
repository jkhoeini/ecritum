# ADR-0023: Lua Support Through LuaJ Spike

Status: Accepted for M5-001 spike; next-release promotion policy superseded by
ADR-025.

ADR-025 supersedes the "not release-ready Core" classification for M9 and later.
This ADR remains the historical LuaJ selection and Lua security-surface record.
Lua can be claimed in the next default artifact only after the M10/M14
conformance, abuse, metrics, license, clean-consumer, and packaged-app gates
pass.

Reviewers: GraalVM Runtime, Architecture Expert, Security, Tests/TDD, Release,
Claude CLI.

## Context

M5 tests whether Ecritum can add Lua scripting without assuming an official
Truffle Lua runtime. The runtime must keep the public C ABI language-neutral:
`ecritum_eval_start` accepts a language string and returns the existing
job/value/error model. Lua must not add public Lua-specific C symbols.

The current artifact already embeds SCI Clojure and GraalJS. ADR-007 records
that the combined artifact is a measured smoke artifact, not a release-ready
Core artifact, because it exceeds ADR-018's current size tripwires. Lua work
therefore needs dependency/license/size/performance evidence before any default
artifact promise.

Primary references reviewed:

- Maven Central `org.luaj:luaj-jme:3.0.1`, MIT license:
  https://central.sonatype.com/artifact/org.luaj/luaj-jme/versions
- Maven Central `org.luaj:luaj-jse:3.0.1`, MIT license and POM shape:
  https://central.sonatype.com/artifact/org.luaj/luaj-jse
- LuaJ README, library scope, Java integration, and compiler notes:
  https://github.com/luaj/luaj
- Rembulan overview, Apache-2.0 license, sandboxing focus, and CPU accounting:
  https://jarcasting.com/artifacts/com.jukusoft/rembulan-parent/
- Cobalt README, re-entrant LuaJ fork, interrupt/resume support, and upstream
  warning against general-purpose use:
  https://github.com/cc-tweaked/Cobalt

Local probes on 2026-06-06:

- `org.luaj:luaj-jme:3.0.1` and `org.luaj:luaj-jse:3.0.1` both fetch from
  Maven Central.
- Both POMs declare `MIT License`.
- `luaj-jse` includes `org.luaj.vm2.lib.jse.LuajavaLib` and Java coercion
  classes; it is not the first-choice artifact for Ecritum.
- `luaj-jme` still contains package, IO, OS, debug, and compiler classes, but
  it omits the JSE Luajava bridge classes.
- A custom `Globals` instance can evaluate `return 40 + 2` with BaseLib and
  LuaC installed.
- Installing `PackageLib`, pure table/string/math libraries, DebugLib, and
  LuaC, then removing `debug`, `package`, `require`, `dofile`, `loadfile`,
  `load`, and `loadstring` from globals before guest evaluation preserves pure
  script support.
- A Java-installed DebugLib count hook interrupts `while true do end` with a
  Lua error after an instruction budget while `debug` and `package` remain
  absent from guest globals.
- Claude plan review found two implementation blockers: LuaJ coroutine threads
  do not inherit the DebugLib count hook, and `StringLib` exposes
  `string.dump`. M5 resolves these by not installing `CoroutineLib` and by
  removing `string.dump` from the guest-visible string table.

## Decision

M5-001 implements Lua as an experimental LuaJ-backed smoke path using
`org.luaj:luaj-jme:3.0.1`.

The public language identifier is `lua`. Swift already has
`EcritumLanguage.lua`; the C wrapper may accept `"lua"` through the existing
`ecritum_eval_start` language parameter.

The public C ABI remains unchanged. The Native Image shared library gains a
private entrypoint, `ecritum_graal_eval_lua_with_stdlib`, matching the existing
Clojure/JavaScript private entrypoint pattern. `docs/abi/ecritum-c-abi.json`
must list that symbol as private if symbol-gate coverage expands in this task.

Lua support is not release-ready Core in M5-001. It is accepted as a measured
local smoke path only if these gates pass:

- Java unit tests for eval/value/error/host/permission/timeout behavior.
- Native shared-library smoke through the public C eval/job/value API.
- Strict Lua conformance provider for eval, host calls, errors, and selected
  standard-library/default-deny behavior.
- Lua security-abuse provider covering default-deny filesystem, network,
  process, environment, class/Java bridge, native loading, raw host object,
  raw C handle, and package/classpath mutation probes.
- Native Image build and XCFramework packaging.
- License report and dependency delta updated for LuaJ.
- Size, startup/RSS, and first-Lua-eval evidence recorded.

If Native Image or security verification fails, M5-001 is still allowed to
finish as a documented deferral, but Ecritum must not claim Lua support.

## Lua Adapter Boundary

Add `LuaEvaluator` as a dedicated adapter. It owns:

- custom LuaJ `Globals` construction
- instruction-budget hook installation
- source/source-name handling
- Lua value normalization to backend values
- Ecritum value conversion for host callback arguments and returns
- structured status/category/language/source-name diagnostics
- `ecritum` table installation for host functions and standard-library facades
- Lua-specific escape-hatch denial tests

The adapter must not use `JsePlatform.standardGlobals()`,
`JmePlatform.standardGlobals()`, `DebugLib` as a guest-visible API,
`LuajavaLib`, `LuaJC`, `CoroutineLib`, or bytecode-to-Java compilation.

## Guest-Visible Lua API

Lua sees one Ecritum-owned global:

```lua
ecritum
```

Registered host namespace functions are projected under nested tables. A host
function registered as namespace `app`, function `notify` is called as:

```lua
return ecritum.app.notify("hello")
```

The first Lua facade scope may include pure JSON/time functions and default
denials for side-effecting facades. Filesystem/network/process/environment
access remains denied unless routed through an Ecritum standard-library bridge.

## Denied LuaJ Surface

Guest code must not access:

- `luajava`
- Java classes or class loaders
- `io`
- `os`
- `debug`
- `package`
- `require`
- `load`, `loadstring`, `loadfile`, `dofile`
- `string.dump`
- `coroutine`
- `package.loadlib`
- arbitrary package path or cpath mutation
- native libraries, subprocesses, environment variables, filesystem, or network
  through LuaJ standard libraries
- Java bytecode generation or Java class emission
- binary chunks

The M5-001 implementation may include LuaJ classes that are unreachable to
guest code if static and abuse tests prove they are not exposed. Future release
work can reduce the bundled classes only after preserving behavior.

## Value Mapping

Lua values map to Ecritum backend values as follows:

| Lua value | Ecritum result |
| --- | --- |
| `nil` | `.null` |
| boolean | `.bool` |
| integral finite number within signed 64-bit range | `.int` |
| finite non-integral number | `.double` |
| non-finite number or overflow | structured `ECRITUM_ERROR_SCRIPT` |
| string | `.string` |
| array-like table with contiguous integer keys starting at 1 | `.array` |
| table with string keys | `.object` |
| nested tables | recursive `.array` / `.object` with cycle detection |
| function, thread, userdata, metatable-dependent unsupported shapes | structured `ECRITUM_ERROR_SCRIPT` |

Mixed array/object tables are rejected in M5-001 unless an implementation plan
defines a deterministic object representation.

## Consequences

- LuaJ is small and MIT-licensed, but old. It needs explicit security gates
  because the jar contains unsafe libraries even when the selected globals hide
  them.
- DebugLib count hooks cover the main Lua thread only in M5. Coroutines are
  omitted because LuaJ creates separate threads that would otherwise bypass the
  instruction budget. Reintroducing coroutines requires a separate reviewed
  design that installs the budget hook on every Lua thread.
- M5 does not implement hard heap accounting for Lua table/string allocation.
  Promotion from experimental to Core requires a resource-limit design and
  verification beyond the instruction budget.
- Rembulan has a stronger sandboxing and CPU-accounting story, but it brings a
  larger multi-module compiler/runtime/stdlib surface and requires a separate
  Native Image proof. It remains the primary fallback if LuaJ cannot satisfy
  timeout/security.
- Cobalt has attractive interrupt/resume support, but its own README says not
  to use it for normal Lua implementations because its API stability and design
  follow CC: Tweaked. It is not selected for Ecritum M5.
- A separate native Lua dylib keeps Lua semantics closer to PUC-Lua, but it
  adds a second native runtime/distribution/signing surface and a larger FFI
  security boundary. It is deferred until the Java-hosted options fail.

## Verification Plan

- `mise exec -- just test-java`
- `mise exec -- just native`
- `mise exec -- just test-native-eval-smoke`
- `mise exec -- just xcframework`
- `mise exec -- just test-swift`
- `mise exec -- just conformance-lua-native`
- `mise exec -- just security-lua`
- `mise exec -- just check-abi`
- `mise exec -- just check-xcframework`
- `mise exec -- just license-report`
- `mise exec -- just check-dep-delta`
- `mise exec -- just size`
- `mise exec -- just bench-cold-start`
- `mise exec -- just bench-idle-rss`
- `mise exec -- just bench-lua-first-eval`

`just size` is expected to remain blocked until ADR-018/Core-Full artifact
classification is resolved. That blocker must be recorded before release
claims.
