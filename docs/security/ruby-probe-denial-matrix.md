# Ruby Probe Runtime Denial Matrix (M12-001C Part A)

Status: preparation-only. This is NOT a Ruby support claim. The `ruby-probe`
profile is private validation infrastructure (see ADR-0027) and must not become
a public API, Swift language support claim, README support-matrix entry,
release-note claim, or default artifact dependency until the new-coordinate
validation path passes all release/security gates.

## Purpose

The Ruby probe (`native/src/ruby-probe/java/ecritum/RubyProbeEvaluator.java`)
denies dangerous operations with two layers:

1. A deny-by-default GraalVM/TruffleRuby `Context` built by `newContext()`
   (`HostAccess.NONE`, `IOAccess.NONE`, `allowNativeAccess(false)`,
   `allowCreateProcess/Thread(false)`, `EnvironmentAccess.NONE`,
   `PolyglotAccess.NONE`, `ruby.platform-native=false`, `ruby.cexts=false`,
   `ruby.rubygems=false`).
2. A lexical regex pre-filter `DENIED_SOURCE_PATTERNS` applied by `evaluate()`
   BEFORE eval.

The recorded security review requires that public Ruby be enforced by the
RUNTIME policy, with the lexical layer only defense-in-depth. The pre-existing
tests tripped the lexical filter before eval and therefore did NOT prove the
runtime context denies these surfaces.

This task evaluates dangerous Ruby DIRECTLY through the production
deny-by-default context (`RubyProbeEvaluator.newContext()`), BYPASSING the
lexical filter, to prove (or disprove) runtime-grade denial honestly.

## How to reproduce

- Java matrix (raw production context, lexical filter bypassed):
  `mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml -Pruby-probe -Dtest=RubyDenialMatrixTest test`
- Existing probe tests:
  `mise exec -- just test-ruby-probe-java`
- C ABI probe (incl. lexical-bypass cases):
  `mise exec -- just native-ruby-probe && mise exec -- just test-ruby-native-probe`

Test source: `native/src/ruby-probe/test/java/ecritum/RubyDenialMatrixTest.java`
and `Tests/C/native_ruby_probe.c`.

Only production change: `RubyProbeEvaluator.newContext()` was changed from
`private` to package-private so the test can build the EXACT production context
and run raw eval. No "trusted"/bypass eval path was added to production code, and
no denial option was relaxed.

## Denial matrix

Runtime evidence is from raw eval through the production context with the
lexical filter bypassed (TruffleRuby `dev.truffleruby:truffleruby:34.0.1`,
Truffle/GraalVM `25.0.2`).

| # | Surface (raw source) | Lexically denied? | Runtime denied (context policy)? | Observed runtime behavior / evidence |
|---|----------------------|-------------------|----------------------------------|--------------------------------------|
| 1 | `Java.type('java.lang.System')` | Yes | Yes | `PolyglotException: Access to host class java.lang.System is not allowed.` (HostAccess.NONE) |
| 2a | `Polyglot.eval('js','1+1')` | Yes | Yes | `PolyglotException: No language for id js found. Supported languages are: [ruby]` (single-language context) |
| 2b | `Polyglot.import('x')` | Yes | Yes | `PolyglotException: java.lang.SecurityException: Polyglot bindings are not accessible for this language.` (PolyglotAccess.NONE) |
| 3a | `File.read('/etc/hosts')` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 3b | `File.write('/tmp/ecritum_probe_x','x')` | Yes | Yes | `PolyglotException: native access is not allowed` (no file created) |
| 3c | `Dir.entries('/')` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 4a | `require 'socket'; TCPSocket.new('127.0.0.1',9)` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 4b | `require 'net/http'` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 5a | `ENV['PATH']` | Yes | Yes | `PolyglotException: native access is not allowed` (real environment never leaks; EnvironmentAccess.NONE) |
| 5b | `ENV.to_h` | Yes | Yes | `PolyglotException: native access is not allowed` (no environment hash leaks) |
| 6a | `system('true')` | Yes | Yes | `PolyglotException: native access is not allowed` (allowCreateProcess false) |
| 6b | `` `true` `` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 6c | `Process.spawn('true')` | Yes (`spawn(`) | Yes | `PolyglotException: native access is not allowed` |
| 6d | `IO.popen('true')` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 6e | `require 'open3'` (require only) | Yes | **NO — GAP** | Returns `true`. The pure-Ruby stdlib loads; runtime does not deny the require itself. See GAP-1. |
| 6e' | `require 'open3'; Open3.popen3/capture2/pipeline('true')` | Yes | Yes | `PolyglotException: native access is not allowed` (the actual process spawn is denied) |
| 7a | `require 'fiddle'` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 7b | `require 'ffi'` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 8a | `Thread.new{1}.value` | Yes | Yes | `PolyglotException: threads not allowed in single-threaded mode` (allowCreateThread false) |
| 8b | `Ractor.new{1}.take` | Yes | Yes | `PolyglotException: uninitialized constant Ractor` (not available in this build) |
| 9a | `require 'rubygems'` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 9b | `Gem` | No (`\bGem\b` not in filter) | Yes | `PolyglotException: uninitialized constant Gem` (ruby.rubygems=false) |
| 9c | `require 'bundler'` | Yes | Yes | `PolyglotException: native access is not allowed` |
| 10a | `require 'openssl'` | Yes | Yes | `PolyglotException: native access is not allowed` (cext-backed) |
| 10b | `require 'bigdecimal'` | No | Yes | `PolyglotException: cannot load such file -- bigdecimal` (not on probe load path) |
| 10c | `$LOAD_PATH` read + `<<` mutate | Yes | **NO — GAP** | Reads back the load-path array; `<<`/`unshift` succeed. See GAP-2. |
| 10c' | `$LOAD_PATH.unshift('/tmp'); require 'on_disk_lib'` | Yes (the `$LOAD_PATH` token) | Yes (the require) | `PolyglotException: cannot load such file` — a mutated load path cannot reach disk because IO is denied |
| 11a | `Object.const_get(:File).read('/etc/hosts')` | Yes (`:File` matches `\bFile\b`) | Yes | `PolyglotException: native access is not allowed` |
| 11b | `Object.const_get("Fil"+"e").read('/etc/hosts')` | **No (true bypass)** | Yes | `PolyglotException: native access is not allowed` |
| 11c | `send(:system,'true')` | **No (true bypass)** | Yes | `PolyglotException: native access is not allowed` (`system,` has no `(`, so the `system\s*\(` pattern does not match) |
| 11d | `send("sys"+"tem",'true')` | **No (true bypass)** | Yes | `PolyglotException: native access is not allowed` |
| 11e | `Kernel.send(:system,'true')` | **No (true bypass)** | Yes | `PolyglotException: native access is not allowed` |
| 11f | `method(:system).call('true')` | **No (true bypass)** | Yes | `PolyglotException: native access is not allowed` |

Notes:

- The dominant runtime guard for filesystem/network/process/env/FFI/gems is the
  single TruffleRuby message `native access is not allowed`, produced by
  `allowNativeAccess(false)` plus `ruby.platform-native=false`. These surfaces
  are genuinely runtime-denied, not merely lexically denied.
- The introspection bypasses in row group 11 are the most important runtime
  proof: string-built constant/method names (`"Fil"+"e"`, `"sys"+"tem"`) are NOT
  matched by `DENIED_SOURCE_PATTERNS`, so they reach the real runtime, and the
  runtime still denies the underlying `File.read`/`system`.

## Honest gaps (surfaces NOT runtime-denied; lexical filter is load-bearing)

These are recorded as gaps for M12-002 to close with runtime policy before any
public Ruby support. Neither is currently weaponizable into an escape, but each
relies on the lexical filter (defense-in-depth) rather than the runtime policy
the security review requires.

- **GAP-1: `require 'open3'` is not runtime-denied.** The pure-Ruby `open3`
  stdlib is already on the probe load path, so `require 'open3'` returns `true`
  at runtime even with `ruby.cexts=false` and `ruby.rubygems=false`. Mitigation
  in place: every operation that actually spawns a process (`Open3.popen3`,
  `Open3.capture2`, `Open3.pipeline`) is runtime-denied with
  `native access is not allowed`. So no process escapes; only the harmless
  require/`respond_to?` succeeds. The lexical filter is what blocks the require
  itself in production today.

- **GAP-2: `$LOAD_PATH` is readable and mutable at runtime.** Reading
  `$LOAD_PATH` (and `$LOADED_FEATURES`) returns real values, and
  `$LOAD_PATH << '/tmp'` / `unshift` succeed. Residual exposure is
  information disclosure of absolute GraalVM cache paths. Mitigation in place: a
  subsequent `require` of an on-disk file still fails with
  `cannot load such file` because IO/filesystem access is denied, so a mutated
  load path cannot be used to load attacker code from disk. The lexical filter
  is what blocks `$LOAD_PATH` in production today.

Recommendation for M12-002: prefer a runtime mechanism (restricted load path /
disabling stdlib `open3` and `$LOAD_PATH` mutation, or an allowlist of
loadable features) so the lexical regex is not the load-bearing control for
these two surfaces.

## Status-classification nuance (C ABI)

When a lexical-bypass surface reaches the runtime through the production
`evaluate()` path, `RubyProbeEvaluator.classify()` maps the TruffleRuby
`native access is not allowed` message to category `runtime` -> status `SCRIPT`
(17), NOT `permission` -> `PERMISSION_DENIED` (14). The denial is real either
way (no value escapes, status is never `OK`), but the C ABI surfaces it as
`SCRIPT` for the runtime-denied bypass cases versus `PERMISSION_DENIED` for the
lexically-denied cases. The C probe asserts this explicitly. M12-002 should
consider folding the runtime native-access guard into the `permission`
classification so the ABI reports denials consistently. This is a reporting
nuance, not an escape.

## Verdict

For every escape surface required by ADR-0027's denial list (RubyGems, Bundler,
native extensions, NFI/FFI, native access, Java access, raw Polyglot access,
process, network, environment, unrestricted filesystem), the underlying
dangerous operation is denied by the RUNTIME context policy when invoked
directly with the lexical filter bypassed — including via string-built
introspection that defeats the regex. Two surfaces (`require 'open3'` and
`$LOAD_PATH` read/mutate) are NOT denied by the runtime require/access boundary
itself and currently rely on the lexical filter; neither yields an escape today
because the operations that would weaponize them (process spawn, on-disk
require) are runtime-denied. M12-002 must close GAP-1 and GAP-2 with runtime
policy before public Ruby support is claimed.

---

# RubyEvaluator (production) prelude hardening — M12-002 security review

This section covers the PRODUCTION evaluator
`native/src/full/java/ecritum/RubyEvaluator.java` (not the probe). The
GraalVM/TruffleRuby RUNTIME policy is the real trust boundary and the review
found NO escape through it (no host class / IO / process / network / env /
native / thread access). The findings below are about the defense-in-depth
PRELUDE (which closes GAP-1 `require` and GAP-2 `$LOAD_PATH`) being bypassable,
plus a few previously-untested surfaces. Every vector was tested empirically
first, then sealed or proven inert.

## What changed in the prelude

The prelude previously REDEFINED `require`/`require_relative`/`load`/`autoload`
(and `Module#autoload`) to raise `SecurityError`. That is insufficient: the
redefined methods are still reachable via `instance_method`/`send`/`bind_call`
(only their bodies raise), and a guest could re-open `module Kernel` and call
`super` to reach the prior definition. The prelude now instead:

1. **Removes the loaders with `undef_method`** on `Kernel` (instance + singleton)
   and `Module#autoload` — `require`, `require_relative`, `load`, `autoload`.
   `undef_method` deletes the method from the lookup chain, so reflection and
   `super` cannot recover the original working loader.
2. **Denies the eval family** (`Kernel#eval`, `BasicObject#instance_eval` /
   `instance_exec`, `Module#class_eval` / `module_eval` / `class_exec` /
   `module_exec`, `Binding#eval`) via `undef_method`, matching
   `PythonEvaluator`'s `eval`/`exec`/`compile` denial. `define_method` is
   intentionally NOT undef'd (the `ecritum` global installer needs it). The host
   polyglot `context.eval(...)` used by `guestBytes`/`installEcritumGlobal` is a
   different API and is unaffected.
3. **Keeps `$LOAD_PATH` / `$LOADED_FEATURES` cleared and frozen** (GAP-2,
   unchanged) — also the backstop that makes any ObjectSpace-reified `require`
   inert.
4. **Neutralizes the runtime fingerprint** by resetting `RUBY_DESCRIPTION` to
   `"ruby"` and `RUBY_PLATFORM` to `"ecritum"` (RISK-2).

Classification: a `NoMethodError` naming one of the SEALED names (loaders + eval
family), including the `super: no superclass method 'require'` phrasing, is
folded into the `permission` category (`PERMISSION_DENIED`, 14) by
`RubyEvaluator.classify()` via `isSealedMethodDenial()`. An ordinary
undefined-method (a guest typo) stays `runtime`/`SCRIPT` (17).

## Eval decision: DENIED (Python parity)

Guest `eval`/`instance_eval`/`class_eval`/`module_eval`/`instance_exec`/
`class_exec`/`module_exec`/`Binding#eval` are DENIED. Rationale:

- It achieves parity with `PythonEvaluator` (which denies `eval`/`exec`/`compile`).
- It does NOT break legitimate sandboxed Ruby: the value model, host callbacks,
  the stdlib facades, value normalization, `Array#pack` (`guestBytes`), lazy
  enumerators, fibers, and `define_method` all continue to work (verified).
- `guestBytes` and `installEcritumGlobal` use the HOST polyglot `context.eval`
  API, which is distinct from the guest `Kernel#eval` method, so denying the
  guest method does not affect them (confirmed: byte round-trip and the
  `ecritum` global still work).
- The only suite breakage from denying eval was two GAP-2 tests that used guest
  `eval(...)` purely as a lexical-bypass vehicle; they were rewritten to use the
  `$:` / `$"` aliases (see below), which bypass the lexical filter without eval.
- Even before denial, eval'd code was already fully constrained by the runtime
  policy (e.g. `eval("send(:system,'true')")` -> `native access is not allowed`),
  so this is defense-in-depth, not the load-bearing control.

## Bypass-vector test results (before -> after)

Runtime: TruffleRuby `34.0.1` (like ruby 3.4.9), GraalVM CE 25.0.2.

| Finding | Vector | BEFORE (redefine prelude) | AFTER (undef prelude) |
|---------|--------|---------------------------|------------------------|
| BLOCKER-1 | `Kernel.instance_method(:require).bind_call(self,'open3')` | reached redefined raiser (permission, but method object still existed) | `NoMethodError: undefined method 'require' for module 'Kernel'` -> PERMISSION_DENIED |
| BLOCKER-1 | `method(:require).unbind.bind_call(...)` / `method(:require).call(...)` | reached redefined raiser | `NoMethodError` (method removed) -> PERMISSION_DENIED |
| BLOCKER-1 | `Kernel.send(:require,…)` / `send` / `__send__` / `public_send` | redefined raiser or `private method` runtime error (inconsistent) | `NoMethodError` -> PERMISSION_DENIED (consistent) |
| BLOCKER-1 | `module Kernel; def require(*); super; end; end; require(...)` | **reached the prior definition via super** | `super: no superclass method 'require'` -> PERMISSION_DENIED |
| BLOCKER-1 | `module Kernel; def require(*); 99; end; end; require('open3')` | returned 99 (guest's own inert require) | returns 99 — **accepted residual**: a guest may define its OWN inert require; it can never reach the real loader (which no longer exists). No feature loads, no capability. |
| BLOCKER-2 | `eval('1+1')` | `=> 2` (worked) | `NoMethodError: undefined method 'eval'` -> PERMISSION_DENIED |
| BLOCKER-2 | `instance_eval` / `class_eval` / `module_eval` / `binding.eval` / `*_exec` | all worked (`=> 2`) | all `NoMethodError` -> PERMISSION_DENIED |
| BLOCKER-2 | `eval("require 'open3'")` | already denied (prelude require raised inside eval) | denied (eval itself removed) -> PERMISSION_DENIED |
| BLOCKER-2 | `eval("send(:system,'true')")` | `native access is not allowed` (runtime-constrained) | eval removed -> PERMISSION_DENIED (n/a) |
| RISK-1 | `Fiber.new{1}.resume` / lazy / `Enumerator#next` | worked (cooperative) | worked — cooperative fibers allowed, single-threaded, no escape |
| RISK-1 | `Fiber.new{ send(:system,'true') }.resume` | `native access is not allowed` | PERMISSION_DENIED — a fiber attempting a denied op is still denied |
| RISK-1 | `Thread.start{1}.value` / `Thread.new{1}.value` | denied (single-threaded) | PERMISSION_DENIED |
| RISK-1 | `Process.fork` | not available (`no-fork`) | not available — never spawns a child |
| RISK-1 | `Ractor.new{1}.take` | unavailable / denied | denied — no working concurrency |
| RISK-3 | `ObjectSpace.each_object(Method){...:require}` then `.call('open3')` | found a `:require` Method object | still findable (stale boot object), but **inert**: `.call` raises `$LOADED_FEATURES is frozen; cannot append feature` — no load, no escape |
| RISK-3 | `ObjectSpace.each_object(UnboundMethod){...}.bind_call(...)` | found | inert (same frozen-features backstop) |
| RISK-3 | `ObjectSpace.each_object(Class){...'File'}.read('/etc/hosts')` | File class reachable | `native access is not allowed` -> PERMISSION_DENIED (runtime boundary holds) |
| RISK-2 | `RUBY_DESCRIPTION` / `RUBY_PLATFORM` | leaked `truffleruby 34.0.1 ... GraalVM CE JVM [arm64-darwin6]` | `"ruby"` / `"ecritum"` (no version banner) |

GAP-2 (unchanged runtime control, retested via alias bypass that defeats the
lexical filter without eval): `$: << '/tmp'`, `$:.unshift('/tmp')`, and
`$" << 'x'` all raise `can't modify frozen Truffle::VersionedArray` -> SCRIPT;
`$:` reads back a frozen empty `[]` (no GraalVM cache paths disclosed).

## Verdict (production evaluator)

Every reviewed vector is either SEALED (BLOCKER-1 loaders, BLOCKER-2 eval family,
guest Thread/Ractor concurrency, RISK-2 fingerprint) or PROVEN INERT (RISK-3
ObjectSpace-reified loader blocked by frozen `$LOADED_FEATURES`; cooperative
fibers cannot perform denied ops). No tested vector reaches host / IO / process /
network / env / native / threads; at worst a guest defines its own inert
pure-Ruby `require` that loads nothing. All 16 `RubyEvaluatorTest` cases pass
(was 11), the full Java suite passes (82 tests), `check-security-static` reports
0 violations, and the Python security baseline passes (26 tests).
