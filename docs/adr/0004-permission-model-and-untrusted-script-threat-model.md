# ADR-004: Permission Model And Untrusted Script Threat Model

Status: Accepted

Reviewers: Security, Architecture Expert, GraalVM Runtime, Tests/TDD,
Clean Code, Claude CLI.

## Context

Ecritum embeds guest language runtimes inside a Swift desktop application's
process. ADR-002 defines the C ABI ownership, lifecycle, handle, and error
rules. ADR-003 defines async jobs and callback reentrancy. ADR-014 defines
timeouts, cancellation, resource limits, and conservative poisoning. ADR-016
defines the Swift policy shape. ADR-019 defines host function registration.
ADR-020 defines versioned deny-by-default config parsing and deliberately
defers real sandbox enforcement to this ADR. ADR-013 defines Java and host
interop exposure policy.

The product goal is useful scripting in a packaged macOS app without requiring
app developers or end users to install GraalVM, a JDK, Python, Ruby, Node, or
Clojure separately. The hard security constraint is that guest scripts must not
gain ambient host authority merely because Ecritum runs inside a trusted app.

GraalVM's official sandbox documentation says sandboxing is not available in
GraalVM Community Edition and recommends external isolation for risks outside
the GraalVM sandbox's scope. Ecritum's default distribution plan currently uses
GraalVM CE. Therefore Ecritum CE must not claim an official GraalVM
untrusted-code sandbox or safe execution of hostile user code.

Primary upstream references used for this ADR:

- GraalVM Sandboxing guide:
  `https://www.graalvm.org/jdk25/security-guide/sandboxing/`
- GraalVM `Context.Builder` API:
  `https://www.graalvm.org/sdk/javadoc/org/graalvm/polyglot/Context.Builder.html`
- GraalVM `HostAccess` API:
  `https://docs.oracle.com/en/graalvm/jdk/25/sdk/org/graalvm/polyglot/HostAccess.html`
- GraalVM `IOAccess` API:
  `https://www.graalvm.org/sdk/javadoc/org/graalvm/polyglot/io/IOAccess.html`
- Apple App Sandbox entitlement reference:
  `https://developer.apple.com/library/archive/documentation/Miscellaneous/Reference/EntitlementKeyReference/Chapters/EnablingAppSandbox.html`

## Decision

Ecritum's M2.5 sandbox model is a defense-in-depth architecture, not a single
trusted primitive.

For GraalVM CE artifacts, Ecritum may support trusted host-authored scripting
and may enforce API-level capability checks, but it must not claim that hostile
or untrusted end-user scripts are safely contained. Untrusted-user scripting is
blocked until one of these later decisions is accepted and verified:

- a non-CE GraalVM sandbox policy such as `SandboxPolicy.UNTRUSTED` is licensed,
  packaged, tested in Native Image, and accepted by release/distribution ADRs
- guest execution moves to an external helper process with OS-level address
  space and signal isolation
- a narrower product claim is accepted that explicitly excludes hostile code and
  is reflected in docs, examples, and API names

Every dangerous capability defaults to denied and remains denied unless the
host grants a narrow Ecritum policy object. Raw GraalVM, Polyglot, JVM, or
language runtime options are never caller input. They are internal
implementation details selected by Ecritum from the validated schema in
ADR-020.

The security boundary is:

```text
guest script
  -> language adapter
  -> Ecritum standard-library capability facade
  -> pure policy decision
  -> named host adapter side effect
```

Guest code must not reach filesystem, network, process, environment, clock,
random, logging, Java/JDK access, host callbacks, reflection, class loading,
native loading, or Polyglot inner contexts except through that chain.

## Threat Model

In scope:

- scripts from end users, documents, plugins, or sync data that may attempt to
  bypass policy
- unauthorized reads or writes through filesystem, network, process,
  environment, reflection, class loading, native loading, Java lookup, or host
  callbacks
- resource exhaustion through CPU loops, recursion, large allocations, large
  input/output, callback floods, or blocked callbacks
- information disclosure through structured errors, raw diagnostics, stack
  traces, paths, process commands, environment values, or guest source leakage
- policy bypass during normal eval, timeout, cancellation, script error,
  callback failure, and cleanup paths

These threats drive the defense-in-depth design. Full containment of hostile
code is not claimed for CE in-process artifacts; the Decision section above
defines the untrusted-scripting block.

Out of scope for in-process CE artifacts:

- hardware memory isolation between host and guest
- protection from native memory corruption, JIT/compiler vulnerabilities, or
  bugs in the host app itself
- side channels such as timing, cache, speculative execution, or shared heap
  observation
- malicious native libraries, JNI, FFI, native Python/Ruby extensions, or
  arbitrary JAR/classpath mutation
- OS-level containment of the host process beyond what the app and platform
  provide

When any out-of-scope risk matters for the product claim, Ecritum must use an
external process or an accepted GraalVM sandbox distribution instead of the CE
in-process artifact.

## Defense Layers

Layer 1: versioned Ecritum config.

ADR-020 config parsing is the only host input channel for policy. Unknown keys,
unknown schema versions, duplicate keys, invalid values, raw backend options,
and context widening fail closed before handles are created.

Layer 2: pure policy decisions.

Permission decisions are pure value transformations over effective runtime,
context, and eval policy. They must not read the filesystem, environment,
clock, network, process table, global registry, host callback state, or
Polyglot context. Side effects occur only after a policy decision returns an
allow result.

Layer 3: language adapters.

Each language adapter starts from a deny-all runtime and installs only the
Ecritum standard-library facade for that language. Language-native escape
hatches are disabled or unsupported until the corresponding language ADR proves
they are denied in Native Image tests.

Layer 4: Ecritum standard library.

Script-visible filesystem, network, process, environment, clock, random, log,
HTTP, JSON, crypto, and host-call APIs are Ecritum facades. They recheck policy
at the point of use and never expose raw JDK classes or raw Polyglot host
objects.

Layer 5: GraalVM Polyglot controls.

Every Polyglot context uses an Ecritum-owned builder. The baseline denies
ambient host authority and raw option control. See "Forbidden Polyglot
Settings" below.

Layer 6: resource limits and recovery.

ADR-014 execution deadlines, output caps, stack/heap/callback limits, and
conservative poisoning apply to permission-denied paths as well as successful
execution. Timeout, cancellation, script error, callback error, and cleanup
paths do not disable permission checks.

Layer 7: macOS App Sandbox.

The macOS App Sandbox and entitlements are defense in depth around the host
process. Ecritum does not rely on them as a substitute for in-process policy
checks. App Sandbox cannot decide which individual guest script may use a host
capability once the app process itself has an entitlement.

## Forbidden Polyglot Settings

The default Polyglot builder policy rejects or omits:

- `allowAllAccess(true)`
- `allowHostAccess(HostAccess.ALL)` or any unrestricted host access policy
- `allowHostClassLookup(...)` predicates that allow arbitrary classes
- `allowHostClassLoading(true)`
- `allowNativeAccess(true)`
- `allowIO(IOAccess.ALL)` or deprecated `allowIO(true)`
- unrestricted custom `IOAccess` that exposes host filesystem or sockets
- `allowCreateProcess(true)`
- `allowCreateThread(true)`
- `allowEnvironmentAccess(EnvironmentAccess.INHERIT)` or broad environment
  access
- `allowPolyglotAccess(PolyglotAccess.ALL)`
- `allowInnerContextOptions(...)` with broad or unreviewed option-key prefixes
- `serverTransport(...)`
- raw `.option(...)` or `.options(...)` from host, user, script, document,
  plugin, environment, JVM system property, or package metadata input
- `Engine` configuration that accepts raw system properties as Polyglot options
- experimental options unless the exact option is internally allowlisted by an
  ADR and covered by static and runtime tests

Internal implementation may use a safer explicit value such as `HostAccess.NONE`,
`HostAccess.SCOPED`, `IOAccess.NONE`, a custom virtual filesystem, or a
GraalVM sandbox policy only when the chosen setting is documented and tested.
No public config key can directly name a Polyglot option.

Thread creation remains forbidden in M2.5. A later language ADR may request a
narrow exception only with a thread model, resource controls, Native Image
abuse tests, and release review.

## Capability Table

Filesystem:

- default: denied
- grant shape: mode plus canonical roots
- enforcement: resolve root and requested path through a race-aware policy
  adapter before opening any file
- forbidden by default: unrestricted host file access, `..` traversal,
  symlink escape, broad home-directory access, security-scoped bookmark use
  without host grant

Network:

- default: denied
- grant shape: scheme, host, and port
- enforcement: recheck redirects and final destination before connecting
- forbidden by default: wildcard hosts, raw sockets, loopback, link-local,
  multicast, and DNS-to-IP broadening unless a later ADR accepts exact rules

Process:

- default: denied
- grant shape: exact executable path and argument/environment/working-directory
  policy, if process support is ever accepted
- enforcement: no shell strings, no `PATH` lookup, no inherited ambient env
- forbidden by default: process creation, shell execution, arbitrary command
  arguments, and process inspection

Environment:

- default: denied
- grant shape: exact variable names
- enforcement: read-through facade only; errors never include values
- forbidden by default: listing all variables, wildcard grants, inherited env
  exposure to guest processes

Clock, random, and log:

- default: denied
- grant shape: explicit facade enablement
- enforcement: facades only; no host object exposure
- forbidden by default: high-resolution timing as an ambient side channel,
  unbounded logs, raw host logger handles

Host functions:

- default: no script-visible function exists until the host registers a
  namespace/function and the language adapter exposes the namespace projection
- enforcement: callbacks receive scoped borrowed values per ADR-002 and
  ADR-019, run under ADR-003 reentrancy rules, and observe ADR-014 callback
  limits
- forbidden by default: raw callback handle access, callback storage after
  return, same-runtime blocking reentry

Java/JDK/host interop:

- default: denied
- grant shape: none in ADR-004; ADR-013 defines curated facades and non-goals
- forbidden by default: Java class lookup, reflection, class loading, native
  library loading, classpath mutation, arbitrary JARs, JNI/FFI/native access

## Filesystem Canonicalization

Filesystem enforcement must happen in Ecritum code before a host file descriptor
is opened.

Required behavior:

- reject empty roots and empty requested paths
- reject embedded NUL bytes before reaching platform APIs
- canonicalize configured roots before storing an effective policy
- canonicalize requested paths relative to the capability root
- reject `..` traversal that escapes the canonical root
- resolve symlinks before allow decisions when the target exists
- for create/write paths where the final component may not exist, canonicalize
  the nearest existing parent and reject if the final path would escape the root
- reject time-of-check/time-of-use prone patterns unless the implementation uses
  descriptor-relative operations or otherwise proves containment
- treat macOS security-scoped bookmarks as host-granted roots, not script
  authority

If a platform cannot provide a race-aware containment check for a requested
mode, that mode stays unsupported for untrusted scripting.

## Network Policy

Network enforcement must happen before any socket or HTTP client operation.

Required behavior:

- compare scheme, host, and port against explicit grants
- normalize host names before comparison
- recheck every redirect before following it
- deny redirects from an allowed host to an unallowed host
- deny raw sockets, local network, loopback, link-local, multicast, and Unix
  domain sockets by default
- define DNS behavior before IP-address grants are accepted
- ensure errors do not leak credentials, tokens, full URLs with secrets, or
  response bodies by default

## Language Escape Hatches

Every language ADR must enumerate and test language-native escape hatches.

Minimum required denials:

- GraalJS: `Java`, `Java.type`, `Java.addToClasspath`, `Polyglot.*`,
  `load`, `read`, `readbuffer`, Nashorn compatibility, `js.scripting`, and
  access to process or filesystem APIs outside Ecritum facades
- GraalPy: native extensions, `ctypes`, `cffi`, native wheels, platform access,
  Java inet access when denied, direct Java imports, and raw Polyglot bindings
- TruffleRuby: native extensions, NFI, direct Java/Polyglot modules, and inner
  contexts
- SCI/Clojure: arbitrary `Class/forName`, arbitrary Java interop, reflection,
  `clojure.java.shell`, unrestricted `clojure.java.io`, unrestricted
  `babashka.fs`, arbitrary dynamic namespace loading, and host object access
- LuaJ: `luajava` or any Java bridge, host filesystem/process/network APIs, and
  raw class lookup

If a language cannot disable an escape hatch in Native Image, that language is
not eligible for untrusted scripting in the default artifact.

## Resource And Recovery Rules

Permission failures are terminal operation failures, not advisory warnings.
They return `ECRITUM_ERROR_PERMISSION_DENIED` with redacted diagnostics.

Resource exhaustion and denied operations must not leave hidden work running.
Timeout, cancellation, callback error, denied cleanup, and backend internal
failure follow ADR-014 recovery classification. A context is poisoned unless
the language adapter proves safe reuse in Native Image tests.

Cleanup code is not privileged guest code. It may release Ecritum-owned
resources, but it may not perform guest-requested filesystem, network, process,
environment, Java, native, or host-callback work after cancellation or timeout.

## Verification Requirements

M2.5-002 must add executable gates named:

- `test-security-static`
- `test-security-abuse`
- `test-security-fuzz` or a documented parser-abuse equivalent

Those tasks join the existing gates:

- `mise exec -- just conformance`
- `mise exec -- just test`
- `mise exec -- just test-c-abi-lifecycle`
- `mise exec -- just test-c-abi-asan`
- `mise exec -- just test-c-abi-host-registration`
- `mise exec -- just test-c-abi-host-registration-asan`
- `mise exec -- just test-c-abi-policy-config`
- `mise exec -- just test-c-abi-policy-config-asan`

Static checks must fail if source, generated config, or native-image build
metadata contains forbidden settings from this ADR outside a test fixture that
asserts rejection.

Abuse tests must cover at least:

- filesystem denied
- network denied
- process denied
- environment denied
- reflection denied
- class loading denied
- native library loading denied
- unrestricted Java lookup denied
- raw Polyglot access denied
- raw host object access denied
- allowed filesystem root inside root succeeds
- allowed filesystem root outside root fails
- redirect to unallowed network destination fails
- each denial during normal eval, timeout, cancellation, callback error, and
  cleanup paths

Parser/fuzz-equivalent tests must cover C config, eval options, source, values,
errors, and callback inputs for invalid UTF-8, oversized inputs, duplicate
keys, bad paths, deep nesting, large arrays, NUL bytes, stale handles, wrong
handle kinds, double destroy, use-after-destroy, and concurrent calls.

Strict conformance is a support-claim gate. No language support can be claimed
until the shared conformance runner in strict mode has zero required pending
permission, timeout, host-call, value, error, lifecycle, and allocation cases
for that language provider.

## Consequences

Ecritum has a clear, conservative security posture before Clojure support
starts. The cost is that several attractive shortcuts remain blocked:
`allowAllAccess`, raw Polyglot options, direct Java/JDK access, native
extensions, broad filesystem/network flags, and untrusted-user scripting on the
CE in-process artifact.

This ADR intentionally makes M2.5-002 a hard gate. Language tasks may build
trusted smoke paths, but they cannot claim untrusted scripting or language
conformance until the static, abuse, parser/fuzz, and strict conformance gates
exist and pass.

M2.5-002 also records the vulnerability-response, hardened-runtime, SBOM, and
CVE-tracking requirements that feed the release security gates and ADR-015.
