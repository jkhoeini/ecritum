# TruffleRuby LLVM-Prune Native-Image Size Spike

> **BANNER — MEASUREMENT SPIKE, PREPARATION-ONLY.** This is a throwaway
> size/build-feasibility measurement to inform the M12-002 decision on whether
> TruffleRuby can fit inside the default single `full` artifact under the 450 MB
> release budget. It is **NOT a support claim**: Ruby is **NOT** added to the
> real `full` profile, no shipping artifact path changed, and the `native/pom.xml`
> edits used here were temporary and have been reverted (verified via
> `jj diff --stat` — the only residual file is this report). The canonical
> artifacts at `build/native/macos-arm64/libecritum.dylib` (366,863,376 bytes)
> and `build/native/full/` were never rebuilt or touched; this spike built only
> into `native/target/` via `mvn` directly, never via `just native-full` or
> `just native-ruby-probe`.

## Question

The prior full-size spike (`docs/release/ruby-full-size-spike.md`) measured a
naive 5-language `full + TruffleRuby` Native Image at **534.68 MB**, exceeding
the 450 MB budget by 84.68 MB. TruffleRuby pulls in the LLVM/Sulong backend
(`org.graalvm.llvm:* 25.0.2`: `llvm-language` jar ~21 MB +
`llvm-language-native-resources` ~56 MB). Ecritum's deny-by-default Ruby context
sets `ruby.cexts=false`, `ruby.platform-native=false`, and
`allowNativeAccess(false)`, so the LLVM/Sulong C-extension backend may be
unnecessary at runtime.

Two questions, in order:

- **Q1.** Can TruffleRuby BUILD and EVALUATE basic Ruby with LLVM EXCLUDED?
- **Q2.** If so, does `full + TruffleRuby-without-LLVM` fit under 450 MB?

## Method (no new Java code)

Reused the existing `ruby-probe` evaluator (`RubyProbeEvaluator`, the
deny-by-default `newContext()`), `@CEntryPoint`
(`RubyProbeEntrypoints#ecritum_graal_eval_ruby_probe`), the Java test
(`RubyProbeEvaluatorTest`), and the C probe (`Tests/C/native_ruby_probe.c`).

1. Backed up `native/pom.xml` to `/tmp/pom.prune.backup`.
2. **Q1 dep change:** added Maven `<exclusions>` to the
   `dev.truffleruby:truffleruby:34.0.1` dependency in the **`ruby-probe`**
   profile, excluding all six `org.graalvm.llvm:*` artifacts (`llvm-native`,
   `llvm-language`, `llvm-language-native`, `llvm-language-native-resources`,
   `llvm-language-nfi`, `llvm-api`).
   - `dependency:tree` confirmed: **WITHOUT** exclusions the tree pulls all six
     `org.graalvm.llvm:*:25.0.2` artifacts; **WITH** exclusions, **zero** LLVM
     artifacts remain (only `dev.truffleruby.internal:{runtime,annotations,shared}`,
     `dev.truffleruby.shadowed:joni`, `dev.truffleruby.internal:resources`).
3. **Q2 dep change:** temporarily added the same TruffleRuby dependency (with the
   identical `org.graalvm.llvm:*` exclusions) to the **`full`** profile and added
   `src/ruby-probe/java` as a second `build-helper` compile source root in `full`
   (so `RubyProbeEntrypoints` + `RubyProbeEvaluator` compile into the full image).
   The `ruby-probe` profile was **not** activated, so its `full`-vs-`ruby-probe`
   enforcer ban never fired.
4. Built Native Images directly via Maven (NOT `just`), into `native/target/`.
5. Restored `native/pom.xml` from backup; confirmed clean revert via
   `jj diff --stat`.

Toolchain: GraalVM CE 25.0.2 (`graalvm-community-25.0.2`, OpenJDK 25.0.2),
native-maven-plugin 1.1.1, Maven 3.9.16, macOS arm64,
`MACOSX_DEPLOYMENT_TARGET=14.0`.

## Q1 result: LLVM IS PRUNABLE — eval + denials INTACT without LLVM

### Q1a — Java probe (JVM, LLVM off the classpath): PASS

```
mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml -Pruby-probe \
  -Dtest=RubyProbeEvaluatorTest test
...
[INFO] Tests run: 4, Failures: 0, Errors: 0, Skipped: 0
[INFO] BUILD SUCCESS
```

All 4 tests passed: `40 + 2` → 42, `'hello'`, `[1,'two',true]`, structured
`raise` error, and every escape-hatch denial (Java.type, Polyglot.eval,
Polyglot::InnerContext, require fiddle/socket/open3/openssl/rubygems/bundler,
Kernel.system, IO.popen, backticks, ENV, File.read, Dir.entries, `$LOAD_PATH`,
Thread.new, Ractor.new). **TruffleRuby boots and evaluates without the LLVM
language on the classpath** — it does not hard-require Sulong to start under the
deny-by-default context. (The only output noise was the standard Truffle
host-JDK native-access / `sun.misc.Unsafe` warnings — not failures.)

### Q1b — standalone ruby-probe Native Image WITHOUT LLVM: BUILD SUCCEEDED

```
MACOSX_DEPLOYMENT_TARGET=14.0 mise exec -- mvn -s .mvn/settings.xml \
  -f native/pom.xml clean package -Pnative,ruby-probe \
  -Decritum.native.mainClass=ecritum.RubyProbeEntrypoints -Dmaven.test.skip=true
...
Finished generating 'libecritum' in 1m 30s.
[INFO] BUILD SUCCESS
```

- `nm -gU native/target/libecritum.dylib | grep ecritum_graal_eval` →
  `_ecritum_graal_eval_ruby_probe` (exported). No `llvm`/`sulong` symbols present.
- Image heap embedded **3,712 resources** (vs ~8,763 in the full+ruby+LLVM
  spike). Peak build RSS only **5.76 GB**; build time **1m 30s**.

### Standalone ruby-probe size: WITH vs WITHOUT LLVM

| Variant | dylib bytes | Decimal MB | vs WITH-LLVM |
|---|---|---|---|
| WITH LLVM (prior spike) | ~225 MB | ~225 MB | baseline |
| **WITHOUT LLVM (this spike)** | **172,740,992** | **172.74 MB** | **~−52 MB** |

### Q1b runtime — C probe against the LLVM-free dylib: PASS

Compiled `Tests/C/native_ruby_probe.c` against the LLVM-free
`native/target/libecritum.dylib` (isolated `/tmp` headers + lib, NOT the
canonical stable dir) following the `test-ruby-native-probe` clang/DYLD pattern.
**Exit code 0** — i.e.:

- `40 + 2` decoded to **42** (magic `0x45435631`, status OK, integer kind, value 42).
- Lexical denials returned **PERMISSION_DENIED** (Java.type, Kernel.system,
  `Object.const_get(:File).read`).
- Lexical-**bypass** surfaces (string-built `"Fil"+"e"`, `"sys"+"tem"`) reached
  the real TruffleRuby runtime through the production ABI and were **denied**
  (mapped to SCRIPT) — no value escaped.

**Conclusion: the LLVM/Sulong backend is fully prunable for Ecritum's
deny-by-default Ruby surface. Eval and the full denial matrix remain intact at
both JVM and native/C-ABI levels.**

## Q2 result: full + TruffleRuby-without-LLVM — BUILD SUCCEEDED, but EXCEEDS 450 MB

```
MACOSX_DEPLOYMENT_TARGET=14.0 mise exec -- mvn -s .mvn/settings.xml \
  -f native/pom.xml clean package -Pnative,full \
  -Decritum.native.mainClass=ecritum.NativeEntrypoints -Dmaven.test.skip=true
...
479.46MB in total image size
Finished generating 'libecritum' in 4m 4s.
[INFO] BUILD SUCCESS
```

### 5-language symbol proof (`nm -gU ... | grep ecritum_graal_eval`)

```
_ecritum_graal_eval_clojure
_ecritum_graal_eval_clojure_with_host
_ecritum_graal_eval_clojure_with_stdlib
_ecritum_graal_eval_javascript_with_stdlib
_ecritum_graal_eval_lua_with_stdlib
_ecritum_graal_eval_python_probe
_ecritum_graal_eval_python_with_stdlib
_ecritum_graal_eval_ruby_probe
```

Clojure (×3) + JavaScript + Lua + Python (+ python_probe) + Ruby — five
language families from one image, **no LLVM/sulong symbols**. Image heap
embedded **8,753 resources**; peak build RSS **18.13 GB**; build **4m 4s**.

### Combined dylib size

| Metric | Value |
|---|---|
| Exact bytes | **479,459,696** |
| Decimal MB | **479.46 MB** |
| MiB | 457.2 MiB |

### Verdict vs the 450 MB budget: EXCEEDS

| Comparison | Value |
|---|---|
| `full + ruby` **WITH** LLVM (prior spike) | 534.68 MB |
| **`full + ruby` WITHOUT LLVM (this spike)** | **479.46 MB** |
| LLVM-prune saving | **−55.22 MB** |
| Release size budget (`scripts/size-artifact.py`, `max_artifact_bytes`) | 450.00 MB |
| **Result** | **EXCEEDS budget by 29.46 MB (~6.5% over)** |
| Canonical 4-language `full` baseline | 366.86 MB |
| Delta vs canonical `full` | +112.60 MB (+30.7%) |

Pruning LLVM recovers ~55 MB and removes the larger portion of the original
85 MB overage — but the image still lands **29.46 MB over** the hard 450 MB cap.
LLVM exclusion alone is **necessary but not sufficient**.

## Q3 note (no third full build): denied/foreign Ruby resources could close the gap

The remaining gap is **29.46 MB**. The embedded resource count (8,753) is
dominated by TruffleRuby's `dev.truffleruby.internal:resources:34.0.1` bundle
(jar 29.55 MB compressed; **90.53 MB / 2,813 files uncompressed**). Inventory of
that bundle (`unzip -l`):

| Resource chunk | Uncompressed | Notes |
|---|---|---|
| Bundled MRI native C-ext binaries (`.so`/`.bundle`/`.dll`), all platforms | **66.94 MB** (57 files) | C-extensions — Ecritum sets `ruby.cexts=false` |
| ↳ foreign-platform trees (`linux/amd64`, `linux/aarch64`) | **57.66 MB** (56 files) | We ship only `darwin/aarch64`; these are unusable |
| ↳ `darwin/aarch64` native libs | 9.44 MB | C-ext binaries; still denied by policy |
| `openssl` (incl. the linux/darwin `openssl.so`/`.bundle` above) | 27.85 MB | denied (`require 'openssl'`) |
| rubygems | 1.64 MB | denied (`ruby.rubygems=false`) |
| bundler | 1.49 MB | denied |
| `net/*` | 1.28 MB | denied (`require 'net/http'` etc.) |
| ffi / socket / fiddle | ~0.74 MB | denied |

The largest, cleanest prune target is the bundled **native MRI C-extension
binaries**: 66.94 MB uncompressed across platforms, of which **57.66 MB is for
foreign platforms** (linux x86-64 + linux aarch64) that a macOS-arm64 artifact
can never load, and **all** of which Ecritum denies via `ruby.cexts=false` /
`platform-native=false`. Even after Native Image compression, dropping the
foreign-platform + denied-cext native binaries via a native-image resource
exclusion config (`-H:IncludeResources` allow-list or `-H:ExcludeResources`)
plausibly recovers well more than the 29.46 MB overage — the linux trees alone
(57.66 MB uncompressed) are a credible >29 MB compressed reduction, before even
touching the denied pure-Ruby stdlib (rubygems/bundler/net/openssl-rb ~6 MB).

**This is an estimate from the resources jar, not a measured build** (the spike
brief reserves a third full build only if needed; the evidence is strong enough
without it). A confirming build would add an `-H:ExcludeResources` regex for
`META-INF/resources/ruby/ruby-home/(linux|darwin/amd64)/.*` (and optionally the
denied stdlib gems) under the `native` profile and re-measure. **Risk:** must
verify that excluding the darwin/aarch64 cext `.so`/`.bundle` files (the 9.44 MB
local-platform set) does not break TruffleRuby boot — the deny-by-default
context never loads them, but TruffleRuby's home-resource verification on
startup should be checked. Excluding only the **foreign-platform** trees
(57.66 MB uncompressed, zero risk — wrong-arch binaries are never mappable) is
the safe first cut and on its own likely clears 450 MB with headroom.

## Recommendation

**PRUNE PATH CONDITIONALLY VIABLE — one more pruning step required before M12-002.**

- **Q1 is unambiguous:** the LLVM/Sulong backend is **prunable**. TruffleRuby
  builds, boots, evaluates `40+2`=42, and enforces the complete denial matrix at
  JVM, native-image, and C-ABI levels with **zero** `org.graalvm.llvm:*`
  artifacts on the classpath. Standalone ruby-probe shrinks 225 MB → 172.74 MB.
- **Q2 shows LLVM-prune alone is insufficient:** `full + ruby` without LLVM is
  **479.46 MB**, still **29.46 MB over** the 450 MB cap (down from 84.68 MB over).
- **Q3 indicates the gap is closable** by additionally excluding TruffleRuby's
  bundled native MRI C-extension resources — 57.66 MB of which is foreign-platform
  (linux) content that a macOS-arm64 artifact cannot use and that Ecritum's
  `ruby.cexts=false` policy denies regardless.

**Proposed path to M12-002:** proceed, but do **not** treat LLVM-exclusion as
the whole fix. The validated next step is to combine the LLVM exclusion with a
native-image resource-exclusion config that drops at minimum the foreign-platform
Ruby native trees (and ideally the denied rubygems/bundler/net/openssl stdlib),
then re-run the `full + ruby` build and confirm it clears 450 MB **with adequate
headroom** (target a ≥10% margin so it also passes the size-warn line). Until
that confirming build measures **< 450 MB**, size remains a release-gating risk:
the two exclusions are necessary, and only a re-measure proves sufficiency.

If the second prune step does not clear 450 MB with headroom, escalate to the
budget-raise-or-ADR decision in `docs/release/ruby-full-size-spike.md`
(raise `max_artifact_bytes` via ADR, or keep Ruby as a separate opt-in artifact).

Build feasibility is **not** a blocker: the 5-language LLVM-free image compiles
in ~4 min at ~18 GB peak RSS.

---
*Generated by the GraalVM / Polyglot Runtime LLVM-prune spike. Measurement only —
Ruby was not added to the shipping `full` profile; `native/pom.xml` was reverted.*
