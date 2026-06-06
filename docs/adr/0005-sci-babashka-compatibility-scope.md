# ADR-005: SCI And Babashka Compatibility Scope

Status: Accepted

Reviewers: GraalVM Runtime, Architecture Expert, Security, Tests/TDD, Release,
Claude CLI.

## Context

Ecritum's first language runtime is Clojure-like scripting for Swift desktop
apps. The product goal is not arbitrary JVM execution. It is a packaged,
embeddable scripting runtime that works through the language-neutral C ABI and
Swift wrapper without requiring app developers or end users to install Clojure,
Babashka, GraalVM, a JDK, or any language runtime.

ADR-003 defines the async eval/job model. ADR-004 defines the permission model
and says every language ADR must enumerate language-native escape hatches.
ADR-013 denies raw Java and host interop. ADR-014 defines resource limits,
cancellation, timeout, and conservative recovery. ADR-016 defines Swift script
and language descriptors. ADR-018 defines size, startup, RSS, first-eval, and
dependency/license gates. ADR-020 defines deny-by-default policy config parsing.

The local seed plan names SCI as the embeddable interpreter and Babashka as the
compatibility target. Upstream Babashka is a fast native Clojure scripting
runtime built with GraalVM Native Image and SCI, but it includes broad scripting
features such as libraries, filesystem, process, pods, project loading, and
threads. SCI itself is configurable: extra namespaces, classes, and allowed or
denied symbols are host-provided configuration. Those properties make SCI a good
fit only if Ecritum owns the configuration and explicitly limits the surface.

This ADR decides the Clojure support scope for M3. It does not implement eval,
add dependencies, or add public C ABI symbols.

## Decision

Use embedded SCI as the Clojure evaluator for M3. Ecritum does not invoke,
bundle, or shell out to the `bb` executable.

The compatibility claim is:

> Ecritum supports a curated SCI-based Clojure scripting subset with selected
> Babashka-like namespaces and behavior documented by ADR-005 tests.

The compatibility claim is not:

> Ecritum is Babashka, supports every Babashka namespace, runs arbitrary
> Babashka projects, or runs arbitrary JVM Clojure code.

M3 exposes Clojure through the existing language-neutral design:

- Swift callers select `EcritumLanguage.clojure`.
- C callers pass the language name `"clojure"` to the generic ADR-003 eval
  entry point.
- Results, arguments, and errors use generic Ecritum values, call accessors, and
  structured error objects.
- Host callbacks are projected from ADR-019 registered namespaces/functions.
- No public symbol, Swift API, header, or manifest entry is Clojure-specific
  except the string language name and documented conformance provider.

ADR-003's async eval/job API is the required implementation path for M3-002.
Older synchronous eval sketches are superseded. A synchronous convenience API
may be added later only as sugar over the same job state machine.

## Dependency And Packaging Policy

M3-002 may add SCI only with exact pinned dependency versions in `native/pom.xml`
or another accepted build input. At ADR acceptance time, the current Clojars
metadata reports `org.babashka/sci` version `0.12.51` with EPL-1.0 licensing
and transitive dependencies including Clojure, edamame, graal.locking, and
sci.impl.types. Implementation must re-check current metadata at the time it
adds dependencies and record exact versions.

Before Clojure support is claimed:

- `license-report` inventories every shipped, build-only, and test-only
  dependency with SPDX identifiers.
- EPL obligations for SCI, Babashka-derived libraries if any, and transitive
  dependencies are accepted by Release review.
- `check-dep-delta` fails on unreviewed shipped dependency changes.
- Native Image metadata is minimized and reviewed. No broad reflection,
  resource, JNI, FFM, native access, fallback image, or classpath metadata is
  allowed unless a later ADR accepts the exact tradeoff.
- Packaged artifacts contain all runtime resources needed by SCI. Consumers do
  not need GraalVM, a JDK, Clojure, Babashka, `bb`, Maven, Clojure CLI,
  `deps.edn`, or a host classpath.
- `just size`, `bench-cold-start`, `bench-idle-rss`, `bench-first-eval`, and
  `check-dep-delta` satisfy ADR-018 Core gates or record an accepted release
  blocker.

## Supported Namespace Scope

M3 starts with a small namespace set. Every namespace listed here must have a
compatibility matrix before support is claimed. Missing vars are documented
compatibility deltas, not bugs in Ecritum.

Initial supported namespaces:

- `clojure.core`: ordinary expression evaluation, binding, functions, control
  flow, persistent data structures, sequence operations, metadata where SCI
  supports it, and printing to Ecritum-captured output.
- `clojure.string`: pure string functions that do not touch host IO.
- `clojure.set`: pure set operations.
- `clojure.walk`: pure data traversal.
- `clojure.edn`: EDN read/write over strings and Ecritum values.
- `ecritum`: reserved Ecritum facade namespace.
- `ecritum.json`: JSON encode/decode over Ecritum values.
- `ecritum.time`: deterministic time facade. Wall-clock access is denied unless
  the host grants the clock capability; pure formatting/parsing may be allowed.
- `ecritum.fs`: permission-checked filesystem facade.
- `ecritum.http`: permission-checked HTTP facade.
- Registered host namespaces: explicit projections of ADR-019 namespaces and
  functions.

Deferred or wrapped namespaces:

- `clojure.java.io` is not exposed raw. Selected behavior may appear under
  `ecritum.fs` only after filesystem policy enforcement exists.
- `babashka.fs` is not exposed raw. Selected behavior may appear under
  `ecritum.fs`.
- `babashka.process` is not exposed in M3. A later process facade must go
  through Ecritum policy and ADR-004 process rules.
- HTTP client libraries are not exposed raw. Selected behavior appears under
  `ecritum.http`.
- `java.time` classes are not exposed directly. Selected behavior appears under
  `ecritum.time` or through converted Ecritum values.
- `clojure.test`, `core.async`, futures, pmap, dynamic classpath helpers,
  Babashka task runner, pods, and project loading are deferred.

## Denied Clojure And SCI Surface

M3 denies these features even when the host grants other capabilities:

- direct Java interop, including `Class/forName`, `import`, `new`, static calls,
  instance method calls on Java objects, reflection, method handles, annotations,
  classloader access, and language equivalents
- SCI `:classes` entries that expose arbitrary classes or `:allow :all`
- raw host objects, raw Java objects, raw Polyglot values, raw C handles, raw
  callback handles, and raw Swift closures
- caller-supplied SCI options, load functions, class maps, namespace maps,
  allowed/denied symbol lists, or host bindings
- runtime classpath, module path, JAR, Maven, `deps.edn`, `add-deps`,
  `add-classpath`, source dependency, service-loader, or bytecode mutation
- Babashka pods and pod registry loading
- `load-file`, arbitrary namespace loading, arbitrary project loading, and
  `require` outside the accepted namespace matrix
- `clojure.java.shell`, raw `babashka.process`, direct subprocess APIs, native
  library loading, JNI/FFI/NFI/FFM, sockets, environment access, and host
  filesystem APIs outside Ecritum facades
- thread creation, futures, pmap, agents, raw executors, or blocking constructs
  unless a later ADR accepts a thread model and resource controls

If SCI or a selected library cannot deny one of these paths in Native Image,
Clojure support remains blocked for any claim that includes untrusted or
hostile user scripts.

For GraalVM CE in-process artifacts, Ecritum still does not claim an official
GraalVM untrusted-code sandbox. Clojure support is trusted host-authored
scripting plus Ecritum capability checks until ADR-004's untrusted-code
conditions are satisfied.

## Host Function Projection

Host functions appear in Clojure only when all of these are true:

- the host registered an ADR-019 namespace/function
- the Clojure adapter has an explicit projection for that namespace
- the namespace does not use an Ecritum reserved prefix
- arguments are read through public `ecritum_call_t` accessors
- return values are produced as public `ecritum_value_t` values
- callback execution follows ADR-003 reentrancy rules and ADR-014 callback
  timeout, queue, and recovery rules

Projection shape:

- a registered namespace `app` with function `notify` is callable as
  `(app/notify value...)`
- a dotted namespace such as `app.tools` maps to Clojure namespace
  `app.tools`
- names are the already-validated ASCII namespace/function names from ADR-019
- no callback receives raw SCI vars, raw Java objects, raw C handles, or host
  closure objects

M3-002 must define value conversion before host-call conformance can pass.
Unsupported argument or result kinds fail with structured Ecritum errors, not
Java exceptions.

## Errors, Source Names, And Diagnostics

Script failures return `ECRITUM_ERROR_SCRIPT` with structured, redacted
diagnostics. Diagnostics include the Ecritum operation, language `clojure`,
source name when provided, and a safe category such as `syntax`, `runtime`,
`permission`, `timeout`, or `callback`. ADR-024 defers public line/column and
stack-frame diagnostics from v0.

Diagnostics must not leak:

- host filesystem paths outside source names explicitly provided by the host
- environment values, tokens, full URLs with secrets, process commands, Java
  class names, host stack traces, raw Java exception objects, or raw C handles

## Resource, Timeout, And Recovery Rules

Clojure eval follows ADR-014. M3-002 must prove:

- eval receives an effective execution deadline
- timeout does not report success
- cancellation does not leave hidden guest work running
- context reuse after timeout/cancellation is not claimed unless SCI proves safe
  reuse in Native Image tests
- permission denial during normal eval, timeout, cancellation, callback error,
  and cleanup paths remains enforced
- output limits apply to printed output
- callback timeout and callback failure follow ADR-014 recovery classification

If safe reuse cannot be proven for a failure mode, the job reports the error and
poisons the context or runtime as required by ADR-014.

## Verification Requirements

M3-002 cannot claim Clojure support until all required cases below pass.

ADR and implementation checks:

- `mise exec -- just test`
- `mise exec -- just native`
- `mise exec -- just xcframework`
- `mise exec -- just test-security-static`
- `mise exec -- just test-security-abuse`
- `mise exec -- just test-security-fuzz`
- `mise exec -- just conformance`
- `python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --strict --provider <clojure-provider>`
- `mise exec -- just size`
- `mise exec -- just bench-cold-start`
- `mise exec -- just bench-idle-rss`
- `mise exec -- just bench-first-eval`
- `mise exec -- just check-dep-delta`
- `mise exec -- just license-report`
- `mise exec -- just license-report-strict`

Clojure eval smoke tests:

- `(inc 41)` returns Ecritum int `42`
- booleans, nil, strings, doubles, data, vectors, maps, nested values, and EDN
  round-trip through Ecritum values
- syntax errors include source name, language, and safe category
- runtime errors produce `ECRITUM_ERROR_SCRIPT`

Namespace matrix tests:

- every supported namespace can be required and has documented accepted vars
- an unsupported namespace fails with a structured script error
- missing vars in a supported namespace are documented compatibility deltas
- raw `clojure.java.io`, raw `babashka.fs`, raw process, raw HTTP, and raw
  time access are absent unless wrapped by Ecritum facades

Host-call tests:

- script calls a registered host function
- host function returns a scalar, vector, map, nil, and error to script
- callback failure maps to `ECRITUM_ERROR_CALLBACK`
- callback reentrancy, lifetime, and use-after-return abuse tests pass

Security denial tests:

- `Class/forName`, `import`, `new`, static calls, reflection, classloaders,
  raw Java interop, SCI `:classes` broad access, raw host objects, raw C handles,
  dynamic namespace loading, `load-file`, classpath/JAR/Maven/add-deps, pods,
  raw process, raw filesystem, raw network, raw environment, and native loading
  are denied
- each denial is represented in the shared security abuse runner for Clojure
  before support is claimed

Distribution tests:

- Swift host smoke runs Clojure eval through the packaged XCFramework
- C host smoke can load the runtime artifact without build-machine paths
- a clean-machine packaged app smoke runs without GraalVM, JDK, Clojure,
  Babashka, `bb`, Maven, Clojure CLI, or external classpath
- artifact inspection shows no broad reflection/resource/JNI metadata and no
  unexpected runtime resources

## Consequences

This ADR gives Ecritum a conservative Clojure path that can ship before broader
language support while preserving the language-neutral C ABI. It deliberately
rejects the most convenient Clojure shortcuts: JVM compiler support, arbitrary
Java interop, dynamic dependencies, raw Babashka filesystem/process APIs, pods,
and project loading.

The tradeoff is that early Ecritum Clojure will be smaller and safer than full
Babashka, but less compatible. Compatibility grows only by adding namespace
matrix entries, facade tests, release/license review, Native Image evidence,
and strict conformance.
