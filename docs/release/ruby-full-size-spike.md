# Ruby-in-`full` Native Image Size / RSS / Build-Feasibility Spike

> **BANNER — MEASUREMENT SPIKE, PREPARATION-ONLY.** This is a throwaway
> size/RSS/build-feasibility measurement to inform the M12-002 decision. It is
> **NOT a support claim**: Ruby is **NOT** added to the real `full` profile, and
> nothing in the shipping artifact path changed. The `native/pom.xml` edits used
> here were temporary and have been reverted; the only residual file is this
> report. The canonical `full` dylib at `build/native/macos-arm64/` and
> `build/native/full/` was never rebuilt or touched (this spike built only into
> `native/target/` via `mvn` directly, never via `just native-full`).

## Question

Does a 5-language Native Image (Clojure + JavaScript + Lua + Python + Ruby) —
i.e. the current default `full` artifact **plus** TruffleRuby — fit within the
release **SIZE BUDGET of 450 MB** (`scripts/size-artifact.py`,
`max_artifact_bytes = 450_000_000`)?

## Method (no new Java code)

Reused the existing `ruby-probe` evaluator + `@CEntryPoint` to force TruffleRuby
into a `full`-shaped shared library:

1. Backed up `native/pom.xml` to `/tmp/pom.backup` (verified identical MD5).
2. Temporarily edited the **`full` profile only** (the `ruby-probe` profile was
   **not** activated, so its `full`-vs-`ruby-probe` enforcer ban never fired):
   - Added dependency `dev.truffleruby:truffleruby:34.0.1` (`<type>pom</type>`,
     same coordinate the `ruby-probe` profile uses). This transitively pulled in
     `dev.truffleruby.internal:*`, `org.graalvm.llvm:* 25.0.2` (incl.
     `llvm-language-native-resources`), and shadowed `joni`/`jcodings`/`antlr4`.
   - Added `src/ruby-probe/java` as an extra `build-helper` compile source root,
     so `RubyProbeEntrypoints` + `RubyProbeEvaluator` compiled into the same
     image alongside `NativeEntrypoints` (Clojure/JS/Lua/Python).
3. Built the Native Image directly via Maven (NOT `just native-full`), with the
   same `mainClass` override the canonical full recipe uses:

   ```
   MACOSX_DEPLOYMENT_TARGET=14.0 mise exec -- mvn -s .mvn/settings.xml \
     -f native/pom.xml clean package -Pnative,full \
     -Decritum.native.mainClass=ecritum.NativeEntrypoints -Dmaven.test.skip=true
   ```

   Note: a first attempt without the `-Decritum.native.mainClass=ecritum.NativeEntrypoints`
   override failed fast (the default `ecritum.NativeCoreEntrypoints` lives in the
   `core` profile's `src/core/java`, not compiled under `full`). This is a harness
   detail, not a Ruby/size finding; the canonical recipe always supplies this
   override. The corrected build succeeded.
4. Measured from `native/target/libecritum.dylib`.
5. Restored `native/pom.xml` from backup; confirmed clean revert via `jj diff --stat`.

Toolchain: GraalVM CE 25.0.2 (`graalvm-community-25.0.2`, OpenJDK 25.0.2),
native-maven-plugin 1.1.1, macOS arm64, `MACOSX_DEPLOYMENT_TARGET=14.0`.

## Result: BUILD SUCCEEDED

The 5-language image built successfully — no OOM, no reachability/link failure.

### Combined dylib size

| Metric | Value |
|---|---|
| Exact bytes | **534,677,072 bytes** |
| Decimal MB | **534.68 MB** |
| MiB (`du -h`) | 510 MiB |

### 5-language symbol proof (`nm -gU native/target/libecritum.dylib | grep ecritum_graal_eval`)

```
00000000047f0330 T _ecritum_graal_eval_clojure
00000000047efe30 T _ecritum_graal_eval_clojure_with_host
00000000047f00b0 T _ecritum_graal_eval_clojure_with_stdlib
00000000047f0590 T _ecritum_graal_eval_javascript_with_stdlib
00000000047f0810 T _ecritum_graal_eval_lua_with_stdlib
00000000047f1160 T _ecritum_graal_eval_python_probe
00000000047f0a90 T _ecritum_graal_eval_python_with_stdlib
00000000047f12d0 T _ecritum_graal_eval_ruby_probe
```

All five language families are exported from one image: **Clojure** (3 symbols),
**JavaScript**, **Lua**, **Python** (+ python_probe), and **Ruby**
(`ecritum_graal_eval_ruby_probe`). Five-language coexistence is proven.

### Build cost (from native-image plugin output)

| Metric | Value |
|---|---|
| Peak RSS | **17.55 GB** |
| Build resources (memory ceiling) | 23.47 GB (34.1% of system memory) |
| Image heap | 325.52 MB — 1,939,042 objects and **8,763 resources** |
| Total native-image generation time | **4m 48s** ("Finished generating 'libecritum' in 4m 48s") |
| Total Maven build time | 04:52 min |

The embedded-resource count rose from ~5,215 (4-language `full`) to **8,763** —
roughly +3,548 resources, consistent with TruffleRuby + LLVM (the LLVM native
resources alone are ~56 MB). Peak build RSS (17.55 GB) is slightly above the
~16 GB observed for the 4-language `full` build.

## Verdict vs the 450 MB budget: EXCEEDS

| Comparison | Value |
|---|---|
| Measured `full+ruby` | 534.68 MB |
| Release size budget (`max_artifact_bytes`) | 450.00 MB |
| **Result** | **EXCEEDS budget by 84.68 MB (~18.8% over)** |
| Canonical 4-language `full` baseline | 366.86 MB |
| Delta vs canonical `full` | **+167.81 MB (+45.7%)** |
| 10%-growth warn threshold over 367.2 MB baseline | 403.92 MB (also exceeded) |

Adding TruffleRuby to `full` adds ~168 MB (TruffleRuby runtime + LLVM backend +
its embedded resources), pushing the artifact ~85 MB past the hard 450 MB cap.

## Recommendation

**Size is a release BLOCKER for naive full integration.** Do **not** proceed
with M12-002 by simply adding TruffleRuby to the `full` profile as measured: the
resulting 534.68 MB artifact fails `scripts/size-artifact.py` (over the 450 MB
hard cap by 84.68 MB, and well over the 403.92 MB 10%-growth warn line).

A budget-raise-or-prune-or-ADR decision is required before full Ruby integration.
Options for the EM / release owners to weigh:

1. **Prune the image** below 450 MB — investigate dropping the LLVM/Sulong
   backend or TruffleRuby resources not needed for the sandboxed eval surface
   (LLVM native + resources are the single largest Ruby-attributable chunk). Must
   re-measure to confirm a pruned image clears 450 MB with adequate headroom.
2. **Raise the budget** via an explicit ADR amendment to
   `scripts/size-artifact.py` (e.g. new cap ≥ ~560 MB with headroom), accepting a
   ~45% larger default artifact and its download/footprint cost.
3. **Keep Ruby out of the default `full` artifact** — ship it as a separate
   opt-in artifact/profile (consistent with the existing `ruby-probe` private
   validation posture), leaving the default `full` at 366.86 MB.

Build feasibility itself is **not** a blocker: the 5-language image compiles on
this toolchain/host in under 5 minutes at ~17.6 GB peak RSS. The blocker is
purely artifact size against the 450 MB budget.

---
*Generated by the GraalVM / Polyglot Runtime size-spike. Measurement only — Ruby
was not added to the shipping `full` profile; `native/pom.xml` was reverted.*
