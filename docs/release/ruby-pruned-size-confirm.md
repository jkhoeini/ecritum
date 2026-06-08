# Ruby Pruned-Size Confirming Spike — full + TruffleRuby vs 450 MB Budget

> **BANNER: This is a MEASUREMENT SPIKE — preparation-only for M12-002.**
> It is **NOT** a Ruby support claim, NOT a release gate, and NOT a product
> commitment. TruffleRuby remains private validation infrastructure
> (`ruby-probe`) until it passes the release gates. The numbers below describe
> a throwaway build used only to decide whether the LLVM + dead-resource
> exclusion path closes the size gap. All edits were reverted; the only
> residual change is this document.

## Question

Does `full + TruffleRuby` fit under the **450 MB** release budget when we
exclude (a) the LLVM/Sulong backend and (b) dead foreign-platform native
resources — producing the exact Native Image build config M12-002 would use?

## Verdict

**STILL OVER — size remains a release blocker needing a policy/ADR/user decision.**

The pruned `full + ruby_probe` shared library measures **479,459,680 bytes
= 457.25 MB**, which is **7.25 MB OVER** the 450 MB cap. Resource exclusion of
the foreign-platform native trees (and, in an escalated second build, the
denied Ruby stdlib/docs/cext-header/gem trees) **did not reduce the dylib size
at all** — it remained byte-for-byte identical to the LLVM-only-excluded
baseline (479.46 MB). The LLVM exclusion alone is what moved the needle (down
from the naive 534.68 MB); the resource exclusions are a no-op on final dylib
size.

> M12-002 **cannot** proceed on size grounds without a policy change. The
> remaining ~7.25 MB (and any safety margin) must come from a different lever
> than resource exclusion — see "Why pruning does not help" and "Options" below.

## Measured results

| Build configuration | dylib bytes | dylib MB | vs 450 MB cap |
|---|---:|---:|---:|
| 4-lang full (canonical, established) | 366,863,376 | 349.87 | −100.13 (under) |
| Naive 5-lang full+ruby (established) | — | 534.68 | +84.68 (over) |
| full+ruby, LLVM excluded (established) | — | 479.46 | +29.46 (over) |
| full+ruby, LLVM excluded **+ foreign-platform `linux/*` resources excluded** | 479,459,680 | **457.25** | **+7.25 (over)** |
| full+ruby, LLVM excluded **+ foreign-platform AND denied-stdlib resources excluded** | 479,459,680 | **457.25** | **+7.25 (over)** |

Note: the 457.25 MB figure here is the **shared-library (`.dylib`) file size on
disk** (479,459,680 bytes / 1,048,576). The Native Image build report's
"479.46 MB in total image size" is the same artifact measured by the builder in
MB and is consistent. Both pruned builds produced an **identical** dylib —
the resource exclusions did not change a single byte of the output.

## Exclusion config used (for M12-002 reuse — verbatim)

### 1. Maven LLVM/Sulong exclusions (`full` profile, on the `dev.truffleruby:truffleruby` dep)

These ARE load-bearing for size (LLVM exclusion is what dropped 534.68 → 479.46 MB)
and are proven safe (TruffleRuby boots/evals/denies without `org.graalvm.llvm:*`
given `ruby.cexts=false`, `ruby.platform-native=false`, `allowNativeAccess(false)`).

```xml
<dependency>
  <groupId>dev.truffleruby</groupId>
  <artifactId>truffleruby</artifactId>
  <version>34.0.1</version>
  <type>pom</type>
  <exclusions>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-api</artifactId></exclusion>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-language</artifactId></exclusion>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-language-native</artifactId></exclusion>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-language-native-resources</artifactId></exclusion>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-language-nfi</artifactId></exclusion>
    <exclusion><groupId>org.graalvm.llvm</groupId><artifactId>llvm-native</artifactId></exclusion>
  </exclusions>
</dependency>
```

(The truffleruby `34.0.1` POM directly declares only `llvm-native`; the other
five are transitive. Listing all six as explicit exclusions is belt-and-braces
and guarantees none of `org.graalvm.llvm:*` reaches the image classpath. The
build log confirmed zero `org.graalvm.llvm:*` jars on the `native-image -cp`.)

### 2. `-H:ExcludeResources` Native Image buildArgs (`native` profile)

> **These are confirmed to be a NO-OP on dylib size.** They are recorded for
> completeness and to document what was tried. They remove resource *entries*
> from the embedded resource table (verified: the targeted path strings drop to
> zero in the dylib) but do not shrink the final macOS shared library.

Foreign-platform-only (definitely-dead; `macOS-arm64` can never load these,
`cexts=false` denies them) — single regex matches both `linux/amd64` and
`linux/aarch64`:

```
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/linux/.*
```

Escalated (denied stdlib / docs / cext headers / gems) — added in build 3,
also a no-op on size:

```
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/gems/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/cext/include/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/rubygems/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/bundler/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/rdoc/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/irb/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/reline/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/net/.*
-H:ExcludeResources=META-INF/resources/ruby/ruby-home/common/lib/mri/openssl/.*
```

Note: `-H:ExcludeResources` is experimental in GraalVM 25.0.2 and emits a
warning that it will require `-H:+UnlockExperimentalVMOptions` in a future
release. Given it does not help size, M12-002 should **not** carry it.

## Resource-tree inventory (from `resources-34.0.1.jar`)

The TruffleRuby resources jar lays out platform trees under
`META-INF/resources/ruby/ruby-home/`:

| Tree | Uncompressed bytes | MB | Status |
|---|---:|---:|---|
| `linux/amd64` | 28,761,255 | 27.42 | foreign — dead |
| `linux/aarch64` | 28,903,361 | 27.56 | foreign — dead |
| **foreign `linux/*` total** | **57,664,616** | **54.99** | excluded in builds 2 & 3 |
| `darwin/aarch64` | 9,444,363 | 9.00 | native platform (cexts denied) |
| `common` | 23,418,172 | 22.33 | platform-independent `.rb`/headers |

The largest *embedded* (common) buckets that build 3 additionally excluded:
`gems` 10.36 MB, `mri/rdoc` 1.84 MB, `cext/include` 1.63 MB, `mri/rubygems`
1.41 MB, `mri/bundler` 1.38 MB.

## Why pruning does not help (the spike's key finding)

The established premise — that ~57.66 MB of foreign-platform MRI C-extension
binaries are embedded in the dylib and removing them reclaims ~57 MB — is
**false for the Native-Image shared-library build with cexts disabled**:

1. With `ruby.cexts=false` / `ruby.platform-native=false`, the foreign-platform
   `.so`/`.bundle` binaries are **never reachable**, so Native Image never
   embeds them into the image heap in the first place. Excluding them removes
   nothing from the output. (Confirmed: foreign-platform path strings already
   contribute ~0 to the dylib; excluding them changed 0 bytes.)
2. The `.rb` / C-header text resources that ARE embedded are a small,
   compressed/deduplicated fraction of the 291.70 MB image heap. Excluding even
   the heaviest denied trees (`gems`, `rdoc`, `rubygems`, `bundler`,
   `cext/include`) — verified removed from the dylib string table — produced a
   **byte-identical** 479,459,680-byte dylib.
3. The 457.25 MB is dominated by **code** and **graph/code metadata**, not Ruby
   resource blobs:
   - Code area **183.27 MB** (38.23%): `python-language` 70.25 MB,
     `runtime-34.0.1` (TruffleRuby) 28.23 MB, `js-language` 26.99 MB,
     `java.base` 13.46 MB.
   - Image heap **291.70 MB** (60.84%): `byte[] general heap data` 75.93 MB,
     `byte[] code metadata` 66.19 MB, `byte[] graph encodings` 29.80 MB.
   - Other data 4.48 MB.

The +84.68 MB that the fifth language (Ruby) adds over the 349.87 MB four-lang
baseline is overwhelmingly **runtime + truffle code and runtime-compiled-method
metadata**, which resource exclusion cannot touch. LLVM exclusion already
clawed back ~55 MB of that; nothing in the resource lever remains.

## nm 5-language proof (escalated build)

`nm -gU native/target/libecritum.dylib` (both builds 2 and 3 identical):

```
_ecritum_graal_eval_clojure
_ecritum_graal_eval_clojure_with_host
_ecritum_graal_eval_clojure_with_stdlib
_ecritum_graal_eval_javascript_with_stdlib
_ecritum_graal_eval_lua_with_stdlib
_ecritum_graal_eval_python_with_stdlib
_ecritum_graal_eval_python_probe
_ecritum_graal_eval_ruby_probe
_graal_create_isolate
_graal_tear_down_isolate
```

All four production languages (Clojure incl. `_with_host`/`_with_stdlib`,
JavaScript, Lua, Python) plus the Ruby probe entrypoint and isolate lifecycle
symbols are present — a genuine 5-language image.

## Ruby eval + denial regression (both pruned builds)

`Tests/C/native_ruby_probe.c` compiled against `native/target/libecritum.dylib`
and run from a clean cwd (`DYLD_LIBRARY_PATH=native/target`):

- `40 + 2` → integer `42` ✅
- `Java.type('java.lang.System')` → PERMISSION_DENIED (lexical) ✅
- `Kernel.system('true')` → PERMISSION_DENIED (lexical) ✅
- `Object.const_get(:File).read('/etc/hosts')` → PERMISSION_DENIED (lexical) ✅
- `Object.const_get("Fil"+"e").read('/etc/hosts')` → SCRIPT (runtime native-access denial via ABI) ✅
- `send("sys"+"tem",'true')` → SCRIPT (runtime denial via ABI) ✅

**Exit code 0** for both the foreign-platform-only build (build 2) and the
escalated denied-stdlib build (build 3). The resource exclusions did **not**
break Ruby boot, eval, or the denial matrix — confirming the excluded trees are
genuinely dead, and reinforcing that they were contributing nothing to the
image.

## Build configuration (exact, for reproduction)

```
MACOSX_DEPLOYMENT_TARGET=14.0 mvn -s .mvn/settings.xml -f native/pom.xml \
  clean package -Pnative,full \
  -Decritum.native.mainClass=ecritum.NativeEntrypoints \
  -Dmaven.test.skip=true
```

(`-Decritum.native.mainClass=ecritum.NativeEntrypoints` is required — the `full`
profile's source roots do not include `src/core/java`, so the default
`NativeCoreEntrypoints` main class is not on the classpath. The `ruby_probe`
entrypoint compiles in as an additional `@CEntryPoint` via `src/ruby-probe/java`
added as a compile source root; the `ruby-probe` profile itself was NOT
activated, avoiding the full↔ruby-probe co-activation enforcer ban.)
GraalVM: `graalvm-community-25.0.2`. TruffleRuby: `34.0.1`. Build time ~4m12s,
peak RSS ~18 GB.

## Options for M12-002 (size lever is NOT resources)

Since resource exclusion is a no-op and we are 7.25 MB over even with LLVM
excluded, closing the gap requires a different decision (out of scope for this
measurement spike, listed for the EM/ADR):

1. **Raise the budget** (policy/ADR change) to ~470–480 MB to admit 5-lang.
2. **Drop a language from the default artifact** to fund Ruby (e.g. ship Ruby
   in a separate distribution), per the single-default-runtime artifact policy
   (ADR-0025).
3. **Code-level size levers** not explored here (e.g. dropping the bundled
   profiler-tool, trimming Python/JS language reach, `-Os`-style options) —
   would need their own spike; uncertain they yield 7.25 MB safely.
4. **Defer Ruby** past M12-002 until one of the above is decided.

This spike's job was only to confirm whether (a)+(b) close the gap. They do
not.
