# ADR-006: JavaScript Support Scope And Node.js Non-Goals

Status: Accepted

Reviewers: GraalVM Runtime, Architecture Expert, Security, Swift DX, Release,
Tests/TDD, Claude CLI.

## Context

M3 proved the runtime model with SCI Clojure: scripts evaluate through the
language-neutral C ABI, return Ecritum values, call explicitly registered host
functions, and reach side effects only through Ecritum-controlled facades.
M4 adds JavaScript through GraalJS after that model has executable conformance
and security gates.

The public Swift API already has `EcritumLanguage.javascript`, and policy
configuration already accepts language allowlists containing `"javascript"`.
The native eval worker is currently Clojure-only and calls a private SCI
entrypoint. M4 must add JavaScript behind the existing eval/job/value/host-call
ABI rather than adding a JavaScript-specific public C ABI.

ADR-004 and ADR-013 require every language ADR to enumerate escape hatches and
deny Java/JDK, host-object, filesystem, network, process, environment, native,
and raw Polyglot access unless a reviewed Ecritum facade grants a narrower
capability. GraalJS is powerful enough to expose Java classes, Polyglot
bindings, host IO, CommonJS packages, and Node compatibility when configured to
do so. Those features are not acceptable as implicit authority for untrusted
Swift app scripts.

Primary references reviewed for M4-001:

- GraalVM JavaScript embedding docs:
  https://www.graalvm.org/jdk25/reference-manual/js/
- GraalVM JavaScript modules and packages docs:
  https://www.graalvm.org/dev/reference-manual/js/Modules/
- GraalVM Node.js vs Java `Context` docs:
  https://www.graalvm.org/jdk22/reference-manual/js/NodeJSvsJavaScriptContext/
- GraalVM JavaScript Java interop docs:
  https://www.graalvm.org/jdk17/reference-manual/js/JavaInteroperability/
- GraalVM Polyglot `Context.Builder` API:
  https://www.graalvm.org/sdk/javadoc/org/graalvm/polyglot/Context.Builder.html

## Decision

Ecritum v0 JavaScript support targets GraalJS embedded through the GraalVM
Polyglot `Context` API inside the Native Image shared library.

The language identifier is `javascript`. Native and conformance providers may
also accept `js` internally only if it normalizes to the public language name
`javascript` in errors and evidence. Public Swift callers use
`EcritumLanguage.javascript`.

M4 targets modern ECMAScript as implemented by the pinned GraalJS version that
matches the project GraalVM version. Ecritum does not define an independent
ECMAScript compatibility matrix in M4. The support claim is:

- scalar/object/array Ecritum value round-trips
- script errors produce structured Ecritum errors with language `javascript`
- registered host functions are callable through `ecritum.<namespace>.<name>`
- Ecritum standard-library facades are available through an `ecritum` global
  shape accepted in this ADR
- denied operations fail closed with `ECRITUM_ERROR_PERMISSION_DENIED`
- the packaged XCFramework runs JavaScript without requiring the consumer app
  to install GraalVM, a JDK, Node.js, npm, or JavaScript packages

M4 does not claim browser APIs, DOM APIs, Web APIs, Node.js compatibility,
CommonJS, npm package loading, ES module loading from disk or URLs, native npm
modules, WASM, timers, worker threads, `fetch`, WebSocket, streams, or
cryptography.

M4 also does not use the historical Native Image `--language:js` flag as a
design commitment. JavaScript support is dependency-driven through the Polyglot
`Context` API and must be proven by the Ecritum shared-library build.

## GraalJS Dependency And Packaging

M4-002 may add GraalJS dependencies only after release review accepts the
license and dependency delta. Because this project builds with GraalVM
Community Edition 25.0.2, the first implementation should prefer the Community
artifact shape documented by GraalVM:

- `org.graalvm.polyglot:polyglot:${graalvm.version}`
- `org.graalvm.polyglot:js-community:${graalvm.version}` with `type=pom`, or
  the exact equivalent required by GraalVM 25.0.2 after a compile-proven
  dependency check

Release decision for M4-002: use `js-community`, not `js`, unless a
compile-proven or license-proven blocker forces a revised ADR. Ecritum currently
uses GraalVM Community Edition, and the first JavaScript artifact must not
switch the shipped runtime to a GFTC-only dependency by accident.

The expected runtime dependency tree from a temporary Maven runtime-scope check
on 2026-06-05 is:

```text
org.graalvm.polyglot:polyglot:25.0.2
  org.graalvm.sdk:collections:25.0.2
  org.graalvm.sdk:nativeimage:25.0.2
    org.graalvm.sdk:word:25.0.2
org.graalvm.polyglot:js-community:pom:25.0.2
  org.graalvm.js:js-community:pom:25.0.2
    org.graalvm.js:js:pom:25.0.2
      org.graalvm.js:js-language:25.0.2
        org.graalvm.regex:regex:25.0.2
        org.graalvm.truffle:truffle-api:25.0.2
        org.graalvm.shadowed:icu4j:25.0.2
          org.graalvm.shadowed:xz:25.0.2
      org.graalvm.truffle:truffle-runtime:25.0.2
        org.graalvm.sdk:jniutils:25.0.2
        org.graalvm.truffle:truffle-compiler:25.0.2
```

Expected scopes:

- shipped: `org.graalvm.polyglot:polyglot`,
  `org.graalvm.sdk:collections`, `org.graalvm.js:js-language`,
  `org.graalvm.regex:regex`, `org.graalvm.truffle:truffle-api`,
  `org.graalvm.shadowed:icu4j`, `org.graalvm.shadowed:xz`,
  `org.graalvm.truffle:truffle-runtime`, `org.graalvm.sdk:jniutils`, and
  `org.graalvm.truffle:truffle-compiler`
- already present build/runtime SDK: `org.graalvm.sdk:nativeimage` and
  `org.graalvm.sdk:word`
- POM-only aggregators: `org.graalvm.polyglot:js-community`,
  `org.graalvm.js:js-community`, and `org.graalvm.js:js`

Expected license expressions from local POM metadata:

- UPL-1.0: `polyglot`, `collections`, `nativeimage`, `word`, `regex`,
  `truffle-api`, `xz`, `truffle-runtime`, `jniutils`, and `truffle-compiler`
- UPL-1.0 OR MIT-style dual-license review required: `js-community`, `js`, and
  `js-language`
- Unicode/ICU license review required: `org.graalvm.shadowed:icu4j`

M4-002 must update:

- `native/pom.xml`
- `scripts/license-report.py`
- `scripts/check-dep-delta.py`
- release/license docs when the dependency inventory changes
- `scripts/inspect-artifact.py` and `scripts/check-xcframework.sh` if GraalJS
  changes artifact contents or symbol expectations
- Native Image metadata static checks for resources, reflection, JNI, proxy,
  services, and broad wildcard additions

No implementation may hide a new shipped dependency from the license report or
dependency delta gate.

JavaScript may remain in the default Core artifact only if M4-002 records size,
cold-start, idle-RSS, and first-JavaScript-eval measurements and either passes
the current ADR-018 budgets or writes a reviewed budget/artifact-split decision
before support is claimed. If the Ecritum shared library becomes too large for
Core, Release and Architecture decide whether JavaScript is Full-only before
M4.5.

## JavaScript Adapter Boundary

Add a dedicated JavaScript adapter, not a branch inside the SCI evaluator. The
adapter owns:

- GraalJS `Context` construction
- source/source-name handling
- JavaScript value normalization to Ecritum backend values
- Ecritum value conversion for host callback arguments and returns
- structured script/error/permission/internal result mapping
- the `ecritum` global object or proxy object
- JavaScript-specific escape-hatch denial tests

The public C ABI remains unchanged for M4. The C eval worker may dispatch to
private Native Image entrypoints by language, such as a private
`ecritum_graal_eval_javascript_with_host` or a private shared polyglot
entrypoint, but no public `ecritum_*` C symbol is added for JavaScript in M4.

The backend result wire format from ADR-021 remains the cross-language contract.
If JavaScript needs different internal value conversion, it must still encode
the same backend result kinds: null, bool, int64, finite double, string, data,
array, object, and structured failures.

JavaScript standard-library facades should reuse the ADR-022 policy manifest
and side-effect bridge where possible. A separate JavaScript-side facade layer
may adapt names to camelCase, but C remains the policy and side-effect
authority for filesystem, clock, HTTP, and future side-effecting facades.

Private JavaScript Native Image entrypoints must stay out of public headers and
out of `docs/abi/ecritum-c-abi.json`. M4-002 must run `check-abi` and include a
negative review that no JavaScript-specific public C symbol was added.

## JavaScript Value And Status Mapping

M4-002 must implement and test this result-conversion matrix:

| JavaScript value | Ecritum result |
| --- | --- |
| `null` | `.null` |
| `undefined` | `.null` |
| boolean | `.bool` |
| integer `number` exactly representable as signed 64-bit | `.int` |
| finite non-integer `number` | `.double` |
| `NaN`, `Infinity`, `-Infinity` | structured `ECRITUM_ERROR_SCRIPT` |
| `bigint` within signed 64-bit range | `.int` |
| `bigint` outside signed 64-bit range | structured `ECRITUM_ERROR_SCRIPT` |
| string | `.string` |
| `ArrayBuffer` or `Uint8Array` | `.data` |
| other typed arrays | structured `ECRITUM_ERROR_SCRIPT` unless accepted by a later ADR |
| array | `.array`, recursively converted with cycle detection |
| plain object | `.object`, own enumerable string keys only, recursively converted with cycle detection |
| `Map`, `Set`, `Date`, `RegExp`, `Error`, class instance, proxy, symbol, function, Promise, host object, or raw Polyglot value | structured `ECRITUM_ERROR_SCRIPT` |
| cyclic object or array graph | structured `ECRITUM_ERROR_SCRIPT` |

Unsupported conversions are script errors, not internal failures, unless the
adapter itself violates its contract. Diagnostics must be redacted by default.

## GraalJS Context Policy

Every M4 JavaScript context must be deny-by-default. The implementation must
compile-prove the exact GraalVM 25.0.2 APIs, but the required policy is:

- `allowAllAccess(false)`
- `allowHostAccess(HostAccess.NONE)` or stricter equivalent
- `allowHostClassLookup(name -> false)`
- `allowHostClassLoading(false)`
- `allowPolyglotAccess(PolyglotAccess.NONE)`
- `allowIO(IOAccess.NONE)` unless a later ADR accepts a custom Ecritum
  filesystem module loader
- `allowNativeAccess(false)`
- `allowCreateProcess(false)`
- `allowCreateThread(false)`
- `allowEnvironmentAccess(EnvironmentAccess.NONE)`
- `allowInnerContextOptions(false)`
- `allowValueSharing(false)` unless a later implementation proves it is needed
  and safe for Ecritum values
- no `allowExperimentalOptions(true)` for M4 runtime contexts
- no Nashorn compatibility, `js.scripting`, CommonJS, or Node options
- no context arguments that expose host paths, environment, or process state

If any required denial cannot be expressed with the GraalVM 25.0.2 API in
Native Image, M4-002 must stop and revise this ADR before claiming JavaScript
support.

## Script-Visible API

JavaScript sees one Ecritum-owned global:

```javascript
ecritum
```

Registered host namespaces are projected as functions under
`ecritum.<namespace>.<function>`. For example, a Swift host function registered
as namespace `app`, function `notify` is called as:

```javascript
ecritum.app.notify("hello")
```

Host callbacks remain synchronous in M4, matching ADR-016 and ADR-019. Returned
host values are converted to JavaScript values and then normalized back through
the Ecritum value model when returned from eval.

Swift `EcritumScript(..., language: .javascript, sourceName: ...)` is the M4
caller API. Source names must propagate into structured JavaScript errors with
language `javascript`.

Host names map exactly to `ecritum.<namespace segments>.<function>` with no case
conversion. A registered namespace `app.tools` and function `notify` projects to
`ecritum.app.tools.notify`.

Projection collision rules:

- `ecritum` is always owned by Ecritum.
- top-level JavaScript keys `json`, `time`, `fs`, and `http` are reserved for
  the standard library, so a host namespace whose first segment is one of those
  names is not projected to JavaScript.
- a projection path may not be both a namespace object and a callable function.
  For example, host function `app/tools` collides with namespace
  `app.tools/notify` because both need `ecritum.app.tools`.
- duplicate projected full paths fail closed before evaluation.
- projection collisions return structured `ECRITUM_ERROR_PERMISSION_DENIED` or
  a more specific reviewed projection error before guest code runs.

Reserved namespace rules from ADR-016 still apply. Host namespaces beginning
with `ecritum`, `java`, `javax`, `sun`, `graal`, or `truffle` are not projected
to JavaScript unless a later ADR grants an explicit standard-library namespace.

The JavaScript standard-library shape for M4 is:

```javascript
ecritum.json.readString(text)
ecritum.json.writeString(value)
ecritum.time.parseInstant(text)
ecritum.time.formatInstant(text)
ecritum.time.now()
ecritum.fs.readText(path)
ecritum.fs.readBytes(path)
ecritum.fs.exists(path)
ecritum.http.request(request)
```

M4-002 may defer individual side-effecting standard-library functions only if
they are present as explicit default-deny functions and the conformance suite
records that scope. HTTP remains default-deny-only until a separate network ADR
accepts redirect, DNS, local-network, timeout, TLS, and diagnostics behavior.

`fetch` is not a global in M4. A future host-backed `fetch` must be accepted by
a network ADR and must be implemented as an Ecritum policy-checked facade, not
as raw GraalJS or host IO.

`console` is optional in M4. If implemented, it must route through an
Ecritum-controlled log adapter, obey the log policy, cap output, and avoid host
stdout/stderr by default. Absence of `console` is acceptable for M4.

Promises and top-level `await` are not supported in M4. Ecritum does not drain
JavaScript Promise jobs or await Promise results before completing
`EcritumContext.eval`. Returning a Promise, using top-level `await`, or depending
on async host callbacks must fail with structured `ECRITUM_ERROR_SCRIPT` until a
later ADR defines event-loop and async-host-callback semantics. The async Swift
`eval` API represents the native job lifecycle, not JavaScript Promise support.

## Node.js And Module Non-Goals

M4 JavaScript runs in Java `Context` mode, not Node.js mode.

Denied and unsupported in M4:

- Node.js executable behavior
- `require`
- CommonJS support and `js.commonjs-require`
- npm package loading
- native npm modules
- Node built-ins such as `fs`, `path`, `http`, `https`, `net`, `tls`, `dns`,
  `child_process`, `worker_threads`, `vm`, `module`, `process`, `buffer`, and
  `crypto`
- ES module loading from filesystem, URLs, or custom Truffle `FileSystem`
- dynamic `import()` that performs host IO or network
- `load`, `read`, `readbuffer`, and other shell-like globals
- browser APIs such as DOM, `window`, `document`, `localStorage`, and browser
  `fetch`

Pure JavaScript library support may be reconsidered after M4 only if bundled
source is supplied by the host app as an explicit script input and does not
require Ecritum to expose raw filesystem, network, process, environment, Node,
or CommonJS capability.

## Required Escape-Hatch Denials

M4-002 must add security abuse cases for JavaScript attempts to use:

- `Java`
- `Java.type`
- `Java.addToClasspath`
- `Packages`
- `Graal`
- `Polyglot.import`
- `Polyglot.export`
- `load`
- `read`
- `readbuffer`
- `require`
- `import("node:fs")`
- `import("fs")` or dynamic import that attempts host loading
- `import("file:///tmp/x.js")`
- `import("https://example.com/x.js")`
- `import.meta` host path or URL leakage
- `process`
- `globalThis.process`
- `Deno`
- `Bun`
- `fetch`
- `XMLHttpRequest`
- `Worker`
- `WebAssembly`
- `Atomics`
- `SharedArrayBuffer` if it can enable worker/shared-memory behavior
- `eval("Java.type('java.io.File')")`
- `eval("import('fs')")`
- `Function("return Java")()`
- `new Function("return this")()` to reach denied globals
- prototype/property traversal that tries to find host objects or raw Polyglot
  values
- host-callback reentry, callback error redaction, callback-scope handle
  use-after-return, and callback return/prototype traversal that tries to expose
  raw host objects

`eval` and `Function` may execute pure JavaScript only. They must not grant
Java, Polyglot, module, IO, environment, process, network, native, or host
object authority.

All must fail with explicit denied/missing capability behavior and without raw
Java stack traces, host paths, environment values, credentials, raw C handles,
or raw Polyglot object diagnostics.

## Resource Limits And Cancellation

M4 JavaScript inherits ADR-014 resource and recovery rules. M4-002 must prove:

- timeout cancels or terminates a JavaScript eval without hidden continued work
- cancellation maps to `ECRITUM_ERROR_CANCELLED`
- script exceptions map to `ECRITUM_ERROR_SCRIPT`
- callback failures map to `ECRITUM_ERROR_CALLBACK`
- permission failures map to `ECRITUM_ERROR_PERMISSION_DENIED`
- context reuse after timeout/cancellation is either proven safe or the context
  is poisoned/fail-closed according to ADR-014

If GraalJS cancellation cannot be made reliable in the current worker model,
M4-002 must either remain incomplete or explicitly narrow the milestone to
trusted JavaScript smoke only in PROJECT.org and `DEBT.md`. It must not claim
untrusted JavaScript support until timeout/cancellation/recovery behavior is
proven in the Ecritum shared-library build.

## Verification Plan

M4-002 must add focused red/green tests before claiming JavaScript support:

- Java unit tests for JavaScript value conversion, context options, host
  function projection, structured errors, and escape-hatch denial
- C/native smoke tests for JavaScript scalar/object/array eval, host calls,
  source-name propagation, unsupported language denial, permission denial,
  timeout/cancellation behavior, and no raw options JSON
- Swift XCFramework smoke tests evaluating JavaScript from
  `EcritumLanguage.javascript`, calling a Swift host function, and observing
  structured JavaScript script errors
- shared conformance cases reused with a strict JavaScript provider for eval,
  host, error, permission, and standard-library categories
- JavaScript-specific conformance cases for Promise/top-level await rejection,
  unsupported result conversions, and source-name propagation if those are not
  covered by generic cases
- manifest catalog/count tests updated for every new JavaScript-specific case
- a strict JavaScript conformance provider with `pending=0` for every claimed
  M4 eval/host/error/permission/stdlib/timeout case
- concrete JavaScript security abuse provider cases for every escape hatch
  listed above; generic matrix coverage alone is not enough
- Native Image static checks showing no broad reflection/JNI/proxy/resource
  wildcard was added for GraalJS
- `just` targets:
  - `test-javascript-java`
  - `test-javascript-native-smoke`
  - `test-javascript-xcframework-smoke`
  - `conformance-javascript-native`
  - `security-javascript`
  - `bench-javascript-first-eval`
  - `test-m4-002`
- size/startup commands:
  - `mise exec -- just size`
  - `mise exec -- just bench-cold-start`
  - `mise exec -- just bench-idle-rss`
  - `mise exec -- just bench-javascript-first-eval`
  - `mise exec -- just inspect`
  - `mise exec -- just check-xcframework`
  - `mise exec -- just check-abi`
  - `mise exec -- just license-report`
  - `mise exec -- just license-report-strict`
  - `mise exec -- just check-dep-delta`

M4 is complete only when `mise exec -- just test-m4-002` passes and
PROJECT.org records size, startup, dependency, license, conformance, and
security evidence.

## Alternatives Considered

- Embed V8 or JavaScriptCore instead of GraalJS. Rejected for M4 because it
  creates a separate runtime/distribution model, a different FFI boundary, and a
  separate value/host-call adapter before the GraalVM architecture is proven
  across two languages.
- Run Node.js compatibility mode or enable CommonJS/npm through GraalJS
  `Context` options. Rejected for v0 because it requires IO/module resolution
  authority, invites Node built-in compatibility claims, and conflicts with
  Ecritum's default-deny host capability model.
- Expose JavaScript through a separate helper process. Rejected for M4 because
  the product goal is an embeddable SwiftPM runtime with a stable C ABI and
  packaged XCFramework; a helper process would require a separate lifecycle,
  IPC, signing, sandbox, and distribution ADR.
- Defer JavaScript until after Lua/Python/Ruby. Rejected because JavaScript is
  the next planned runtime after Clojure and is needed before M4.5 ABI freeze to
  prove the C ABI is genuinely language-neutral.
- Accept only a trusted JavaScript demo with no security gates. Rejected as a
  support claim. A trusted-only smoke may exist as temporary evidence only if
  PROJECT.org and `DEBT.md` explicitly say M4 JavaScript is not yet untrusted
  script support.

## Consequences

This keeps the JavaScript milestone aligned with the language-neutral Ecritum
runtime model instead of turning Ecritum into an embedded Node.js distribution.
It also avoids exposing Java authority merely because GraalJS can provide it
when configured differently.

The cost is that many JavaScript ecosystem expectations are non-goals in v0:
there is no npm, Node built-ins, filesystem module loading, browser APIs, or
global `fetch`. M4 deliberately optimizes for safe embeddable scripting in
Swift desktop apps, not general JavaScript application hosting.

GraalJS may significantly increase artifact size and startup/first-eval cost.
That cost is accepted only after M4-002 records measured deltas and release
review decides whether JavaScript remains in the default artifact through
M4.5.
