# ADR-013: Java And Host Interop Exposure Policy

Status: Accepted

Reviewers: Security, Architecture Expert, GraalVM Runtime, Tests/TDD,
Clean Code, Claude CLI.

## Context

ADR-004 defines the permission model, untrusted-script threat model, and
sandbox enforcement architecture. ADR-016 defines Swift policy values and
host registration ergonomics. ADR-019 defines namespace/function registration
without eval. ADR-020 defines deny-by-default config parsing.

Ecritum's implementation language and many useful capabilities are Java/JDK
backed inside a GraalVM Native Image. Guest languages must not inherit that Java
authority. Direct Java lookup, reflection, class loading, native library
loading, classpath mutation, or host object access would bypass the
language-neutral C ABI, the Swift policy shape, the standard-library facade,
and the shared conformance suite.

This ADR decides what Java/JDK and host interop surface guest scripts may see.
It does not add public C ABI symbols and does not implement a language adapter.

## Decision

Guest scripts have no direct Java or host interop by default.

The only script-visible host surface is the Ecritum standard library plus host
functions that the embedding app explicitly registers through ADR-019. Java/JDK
classes may be used internally to implement those facades, but they are not
directly exposed to scripts.

Denied for every language by default:

- arbitrary Java type lookup
- `Java.type`, `Java.addToClasspath`, `Class.forName`, `ClassLoader`, or
  language equivalents
- Java reflection, method handles, invokedynamic handles, annotations, and
  reflective metadata that can reach forbidden classes
- classpath, module path, Maven, JAR, bytecode, resource, or service-loader
  mutation at runtime
- native library loading through `System.load`, `System.loadLibrary`, JNI, FFI,
  NFI, FFM, `ctypes`, `cffi`, Ruby C extensions, native Python wheels, or Lua
  Java bridges
- raw `org.graalvm.polyglot.Context`, `Engine`, `Value`, `Source`, `HostAccess`,
  `IOAccess`, `PolyglotAccess`, or inner-context creation
- raw host objects, raw Swift objects, raw C handles, and raw callback handles
- `HostAccess.ALL` or unrestricted public method/field access

There is no M2.5 public opt-in flag for these features. Any future exception
requires a new ADR, release/license review where relevant, and strict negative
tests.

## Curated Facades

Ecritum may expose useful JDK-backed behavior only through stable Ecritum
facades. Facades must:

- live under reserved Ecritum-controlled namespaces/modules/tables
- accept and return Ecritum values, not raw Java objects
- perform policy checks before side effects
- keep diagnostics redacted by default
- avoid leaking host paths, environment values, process commands, network
  secrets, Java class names, stack traces, or raw exceptions
- be covered by the shared conformance suite before a language support claim

Allowed internal JDK use does not imply script-visible class access. For
example, an Ecritum filesystem facade may internally use `java.nio.file.Path`,
but scripts receive Ecritum value objects and errors, not a `Path` instance.

## Per-Language Surface

Clojure through SCI:

- allowed: a curated SCI namespace set accepted by ADR-005
- possible internal facade projections: `ecritum.fs`, `ecritum.http`,
  `ecritum.json`, `ecritum.time`, and registered host namespaces
- denied by default: arbitrary JVM interop, arbitrary `new`, arbitrary static
  method calls, `Class/forName`, reflection, dynamic class loading,
  `clojure.java.shell`, unrestricted `clojure.java.io`, unrestricted
  `babashka.fs`, arbitrary namespace loading, and raw Java object exposure
- compatibility target: Babashka-like behavior only where ADR-005 lists and
  tests it

JavaScript through GraalJS:

- allowed: an `ecritum` global or module accepted by ADR-006
- denied by default: `Java`, `Java.type`, `Java.addToClasspath`,
  `Polyglot.import`, `Polyglot.export`, inner contexts, Nashorn compatibility,
  `js.scripting`, host class lookup, filesystem/network/process globals not
  owned by Ecritum, and Node compatibility unless ADR-006 separately accepts it

Lua through LuaJ or another accepted runtime:

- allowed: an `ecritum` table accepted by ADR-007
- denied by default: `luajava`, direct Java bridge APIs, host filesystem,
  process, network, and native-loading APIs outside Ecritum facades

Python through GraalPy:

- allowed: an `ecritum` module accepted by ADR-008
- denied by default: Java imports, raw Polyglot bindings, `ctypes`, `cffi`,
  native wheels/extensions, subprocess, raw sockets, direct host filesystem,
  and platform/native POSIX access unless ADR-008 proves denial or a narrow
  facade

Ruby through TruffleRuby:

- allowed: an `Ecritum` module accepted by ADR-009
- denied by default: Java/Polyglot modules, NFI, C extensions, native gems,
  subprocess, raw sockets, direct host filesystem, and inner contexts unless
  ADR-009 proves denial or a narrow facade

Arbitrary JVM languages:

- not an MVP feature
- no Kotlin/Scala/Groovy/JAR/classpath execution claim
- no Maven or JAR loading from scripts
- no Espresso/full-JVM support without a separate ADR, license review, artifact
  budget, and conformance suite

## Host Function Exposure

Host functions are exposed only through validated Ecritum namespaces and
function names from ADR-019. Names beginning with reserved runtime prefixes
remain rejected.

Language projections are deterministic mappings from the same registration
model. A language adapter may expose a registered function only after:

- the function namespace is registered by the host
- the language adapter implements an explicit projection for that namespace
- callback argument and return value conversion are implemented through
  Ecritum values
- callback execution follows ADR-003 reentrancy rules and ADR-014 callback
  resource limits
- callback lifetime rules prevent storing borrowed `ecritum_call_t` or raw
  host values after return

Host callbacks are trusted host code, but guest access to them is not ambient.
No script receives a raw function pointer, raw Swift closure, raw Java object,
or raw C handle.

## Native Image And Packaging Policy

Native Image closed-world compilation is packaging, not a Java sandbox. It
reduces what is present in the artifact, but it is not a substitute for policy
checks.

Default artifact policy:

- no fallback image
- no runtime classpath/JAR loading support
- no broad reflection metadata
- no broad resource metadata that exposes implementation internals
- no JNI/FFM/native access metadata except an internal build need accepted by
  ADR
- no `--enable-native-access` for guest-visible code
- no generated reflection/resource/JNI config from an agent trace without
  review, minimization, and tests

If a language runtime requires metadata or native access that weakens this
policy, the language inclusion ADR must record the tradeoff and may block that
language from the default artifact.

## Verification Requirements

M2.5-002 and later language tasks must add static and runtime checks for this
ADR.

Static checks must reject:

- `HostAccess.ALL`
- `allowAllAccess(true)`
- broad `allowHostClassLookup`
- `allowHostClassLoading(true)`
- raw `.option` or `.options` passthrough from config, env, system properties,
  user files, package metadata, or scripts
- `Java.type`, `Java.addToClasspath`, `Class.forName`, `ClassLoader`, and
  dangerous reflection entry points outside negative-test fixtures
- `System.load`, `System.loadLibrary`, JNI/FFI/NFI/FFM/native extension
  enablement outside an accepted ADR
- Native Image configs that add broad reflection/resource/JNI metadata

Runtime abuse tests must prove denial for every supported language projection:

- Java lookup
- reflection
- class loading
- native loading
- classpath/JAR mutation
- raw Polyglot access
- raw host object access
- raw C handle access
- callback-scope handle use after callback return

The shared conformance suite must stay the cross-language claim gate. A
language provider cannot be called conformant until strict conformance has zero
required pending cases for host calls, denied Java lookup, reflection, class
loading, native loading, values, structured errors, timeout, lifecycle, and
allocation.

## Consequences

This ADR keeps Ecritum's public surface language-neutral and capability-based.
It also blocks several shortcuts that would make early demos easier:
`allowAllAccess`, direct `Java.type`, raw `HostAccess`, raw Polyglot values,
JAR loading, arbitrary JVM languages, and native extension ecosystems.

Future language ADRs can still expose useful standard-library behavior, but
they must do it through Ecritum facades with explicit permissions, negative
tests, artifact/license review where needed, and strict conformance evidence.
