# ADR-0028: Ruby Default-Artifact Size Budget and TruffleRuby LLVM Exclusion

Status: Accepted
Date: 2026-06-09

## Context

M12 adds Ruby (TruffleRuby `dev.truffleruby:truffleruby:34.0.1`) to the single
default Ecritum runtime artifact (ADR-0025). M12-001C proved the runtime-grade
denial matrix (see `docs/security/ruby-probe-denial-matrix.md`) and that the
Ruby payload embeds its resources and runs cwd-independently. The remaining gate
was release size.

Three measurement spikes (recorded under `docs/release/`) established:

- A naive 5-language Native Image (Clojure + JavaScript + Lua + Python + Ruby)
  builds in ~4m48s at ~17.6 GB peak RSS and is **534.68 MB**
  (`ruby-full-size-spike.md`). Build feasibility is not a blocker.
- Excluding TruffleRuby's LLVM/Sulong backend (`org.graalvm.llvm:*:25.0.2`) is
  safe because Ecritum denies C-extensions and native access
  (`ruby.cexts=false`, `ruby.platform-native=false`, `allowNativeAccess(false)`):
  TruffleRuby still boots, evaluates, and enforces every denial without LLVM.
  This reduces the 5-language image to **479.46 MB**
  (`ruby-llvm-prune-spike.md`).
- Native Image `-H:ExcludeResources` pruning of TruffleRuby's bundled native
  trees is a **no-op** on image size: with `cexts=false` those binaries are not
  embedded, and image size is dominated by code area (~183 MB) plus image-heap
  metadata (~291 MB), not Ruby resources. The pruned 5-language image stays at
  **457.25 MB** (`ruby-pruned-size-confirm.md`).

The 4-language default artifact baseline is 366.86 MB. The prior size budget
(`scripts/size-artifact.py`) hard cap was 450 MB, set for the 4-language
artifact. The 5-language artifact at 457.25 MB exceeds it by 7.25 MB (1.6%).

This is a release-safety / distribution decision (it changes the default
artifact every consumer downloads), so per the M12 definition of done it was
escalated to the project owner.

## Decision

1. **Raise the default-artifact size budget to 800 MB.** The owner accepted a
   generous ceiling so the five-language single default artifact (ADR-0025) is
   not size-blocked and has ample headroom for future runtime/CPU updates. The
   five-language requirement and the single default artifact are both preserved;
   no language is dropped and Ruby is not split into a separate artifact.

2. **Exclude TruffleRuby's LLVM/Sulong backend from the shipped artifact.** The
   six `org.graalvm.llvm:*:25.0.2` artifacts are excluded from the
   `dev.truffleruby:truffleruby` dependency in the build. This is proven safe
   under Ecritum's deny-by-default Ruby policy (cexts and native access are
   denied), keeps the artifact ~55 MB smaller, and removes the LLVM bitcode
   interpreter attack surface that Ecritum never exposes. TruffleRuby therefore
   ships in pure-Ruby mode.

3. **Do not carry `-H:ExcludeResources` for TruffleRuby native trees.** It was
   measured to be a no-op for this configuration and adds maintenance cost for no
   benefit.

## Consequences

- `scripts/size-artifact.py` budgets are raised: `max_artifact_bytes` to
  800 MB, `max_private_runtime_bytes` and `warn_artifact_bytes` proportionally.
  When Ruby is integrated (M12-002) the `baseline_artifact_bytes` and
  `DEFAULT_INCLUDED_RUNTIMES`/`PROFILE_RUNTIMES` are updated to the measured
  five-language values and `ruby`.
- The shipped Ruby dependency set EXCLUDES `org.graalvm.llvm:*`. The M12-004
  Ruby-candidate inventory (`docs/release/ruby-candidate-inventory.md`) listed
  LLVM as candidate-shipped; the actual shipped set drops the six LLVM artifacts.
  The release tooling baselines (license-report, check-dep-delta, license-text
  policy, SBOM, notices) must reflect the LLVM-excluded shipped set when Ruby is
  promoted from candidate to shipped in M12-002/M12-003. New shipped SPDX ids
  (`BSD-3-Clause` from antlr4 + truffleruby, `BSD-2-Clause` from truffleruby)
  still apply; they no longer depend on LLVM.
- A larger default artifact (~457 MB vs 367 MB) is accepted for all consumers,
  including those not using Ruby. This is proportionate for an embedded GraalVM
  polyglot Native Image and consistent with the single-default-artifact policy.
- The `ruby-probe` Maven profile and its enforcer ban remain until M12-001D
  retires them; M12-002 adds Ruby to the `full` profile directly (LLVM excluded)
  rather than activating `ruby-probe`.

## Alternatives Considered

- Keep the 450 MB cap and code-prune (drop profiler-tool, trim reachability) to
  fit. Rejected: uncertain to recover 7.25 MB with headroom; the owner preferred
  a generous budget over fragile size surgery.
- Ship Ruby as a separate opt-in artifact. Rejected: reverses ADR-0025's single
  default artifact decision and adds packaging/release complexity.
- Defer Ruby. Rejected: the owner chose to proceed with five languages.
- Keep LLVM/Sulong in the artifact. Rejected: unnecessary under the deny-all
  policy and adds ~55 MB plus a native-code-interpreter attack surface.

## Verification

- `docs/release/ruby-full-size-spike.md` (naive 5-language = 534.68 MB, builds).
- `docs/release/ruby-llvm-prune-spike.md` (LLVM excluded, Ruby still
  boots/evals/denies; 479.46 MB; standalone probe 225 -> 173 MB).
- `docs/release/ruby-pruned-size-confirm.md` (resource exclusion no-op; 457.25 MB;
  C-probe eval + denials intact; clean revert).
- `docs/security/ruby-probe-denial-matrix.md` (runtime-grade denial; LLVM-free
  build keeps all denials).

## Reviewers

- Engineering Manager: accepted after three measurement spikes narrowed the
  decision to a single size gate.
- Project owner: chose to raise the budget to 800 MB.
- GraalVM Runtime: LLVM exclusion proven safe; resource exclusion no-op.
- Release/Licensing: shipped Ruby set excludes LLVM; release baselines updated
  in M12-002/M12-003.
