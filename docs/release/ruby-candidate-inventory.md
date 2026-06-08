# Ruby-Candidate Release-Tooling Inventory (M12-004) — SUPERSEDED

> **SUPERSEDED (M12-002 Slice 2, ADR-0028).** Ruby is now a **default shipped
> language** (Clojure + JavaScript + Lua + Python + Ruby) in the single default
> artifact, integrated via `dev.truffleruby:truffleruby:34.0.1` in the `full`
> profile **with the TruffleRuby LLVM/Sulong backend EXCLUDED** per ADR-0028.
> The candidate model below is retained only as history; its central
> assumptions are now WRONG:
>
> - **LLVM is NOT shipped.** The six `org.graalvm.llvm:*:25.0.2` artifacts are
>   excluded from the build; they are not in the shipped inventory.
> - **antlr4 is NOT shipped.** `org.graalvm.shadowed:antlr4` is transitive only
>   under the excluded `llvm-language`, so it does not reach the default
>   artifact.
> - **Ruby is no longer "blocked/candidate-only."** The shipped Ruby set is the
>   **seven net-new** coordinates (`dev.truffleruby:truffleruby`,
>   `dev.truffleruby.internal:{runtime,resources,annotations,shared}`,
>   `dev.truffleruby.shadowed:joni`, `org.graalvm.shadowed:jcodings`), now
>   carried in the DEFAULT `license-report.py`, `check-dep-delta.py` baseline,
>   `check-license-texts.py` policy, `THIRD_PARTY_LICENSES/` bundle/manifest,
>   `THIRD_PARTY_NOTICES.md`, and the published SBOM.
> - The `--artifact-kind ruby-candidate` mode, its `ruby-candidate-*` justfile
>   recipes, and the separate `THIRD_PARTY_LICENSES/ruby-candidate/` bundle were
>   **removed**; the BSD-2-Clause/BSD-3-Clause texts were promoted into the
>   default bundle and EPL-2.0 was re-provenanced to TruffleRuby in the default
>   policy.
>
> The authoritative shipped Ruby inventory is whatever
> `SOURCE_DATE_EPOCH=0 python3 scripts/license-report.py` emits today. The
> historical content below is preserved for the M12-004 audit trail only.

---

## Historical (M12-004) candidate inventory — NOT the shipped reality

> The text in this section describes the M12-004 *candidate* model, which
> INCLUDED LLVM and antlr4 as "shipped-if-accepted." That is **incorrect for the
> actual shipped artifact** (LLVM and antlr4 are excluded). See the SUPERSEDED
> banner above for the real shipped set.

> **NOT A SUPPORT CLAIM (historical).** This document was PREPARATION ONLY. The
> inventory existed so the release/licensing/security tooling could recognize,
> attribute, and monitor the `dev.truffleruby:truffleruby:34.0.1` candidate
> dependency graph *if and when* the M12 validation gates passed.

## How this inventory is produced

The inventory is generated, never hand-authored. SPDX expressions below are the
verbatim `licenseConcluded` strings emitted by:

```
SOURCE_DATE_EPOCH=0 python3 scripts/license-report.py --artifact-kind ruby-candidate
```

`spdx_license_expression` dedupes and joins POM-declared licenses **in POM
order** with ` AND ` — it is never alphabetized. Candidate behavior activates
only with the explicit `--artifact-kind ruby-candidate` flag (argparse default is
`"default"` in every script). Default-mode output is byte-for-byte unchanged.

Candidate tooling is run via the `ruby-candidate-*` justfile recipes, which are
**excluded from the default `test` / `perf-baseline` / `test-m*` release gates by
design**.

## The 14 candidate additions (historical — rows 7-14 are NOT shipped)

> **Correction (ADR-0028).** Of the 14 coordinates below, only rows **1-6 and
> 13** (the six `dev.truffleruby:*` plus `org.graalvm.shadowed:jcodings`) are
> actually SHIPPED. Rows **7-12** (the six `org.graalvm.llvm:*`) and row **14**
> (`org.graalvm.shadowed:antlr4`) are **EXCLUDED** from the shipped artifact and
> must not be treated as shipped. The shipped SPDX strings for rows 1-6 and 13
> are unchanged and now live in the DEFAULT `check-dep-delta.py` baseline.

Verified against the local Maven repository POMs (read directly from `~/.m2`).

| # | Maven coordinate | Version | SPDX (POM order, deduped) | Shipped? |
|---|---|---|---|---|
| 1 | dev.truffleruby:truffleruby (pom) | 34.0.1 | `EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT AND UPL-1.0 AND ICU` | YES |
| 2 | dev.truffleruby.internal:runtime | 34.0.1 | `EPL-2.0 AND BSD-3-Clause AND BSD-2-Clause AND MIT` | YES |
| 3 | dev.truffleruby.internal:resources | 34.0.1 | `EPL-2.0 AND MIT AND BSD-2-Clause AND BSD-3-Clause` | YES |
| 4 | dev.truffleruby.internal:annotations | 34.0.1 | `EPL-2.0` | YES |
| 5 | dev.truffleruby.internal:shared | 34.0.1 | `EPL-2.0` | YES |
| 6 | dev.truffleruby.shadowed:joni | 34.0.1 | `MIT` | YES |
| 7 | org.graalvm.llvm:llvm-native (pom) | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 8 | org.graalvm.llvm:llvm-api | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 9 | org.graalvm.llvm:llvm-language-nfi | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 10 | org.graalvm.llvm:llvm-language-native | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 11 | org.graalvm.llvm:llvm-language | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 12 | org.graalvm.llvm:llvm-language-native-resources | 25.0.2 | `BSD-3-Clause` | NO (excluded, ADR-0028) |
| 13 | org.graalvm.shadowed:jcodings | 25.0.2 | `MIT` | YES |
| 14 | org.graalvm.shadowed:antlr4 | 25.0.2 | `BSD-3-Clause` | NO (transitive under excluded llvm-language) |

The `truffleruby` artifact is `pom`-packaging (no `.jar`); its license is read
from the version POM at
`~/.m2/repository/<group-as-path>/<artifact>/<version>/<artifact>-<version>.pom`.

LLVM and the `org.graalvm.shadowed` coordinates stay on the existing GraalVM
`25.0.2` line (Truffle/LLVM alignment per ADR-0027); only the `dev.truffleruby:*`
coordinates are at `34.0.1`.

## New SPDX identifiers vs the default artifact

Only **two** SPDX identifiers are new relative to the default shipped artifact:

- `BSD-3-Clause`
- `BSD-2-Clause`

`MIT`, `EPL-2.0`, `UPL-1.0`, and `ICU` are already present in the default shipped
SBOM, so they introduce no new SPDX ids. (`EPL-2.0` is currently only a *test*
scope license in the default artifact — see the provenance finding below.)

## New vs reused license texts

- **Genuinely new texts (must be added):** `BSD-2-Clause`, `BSD-3-Clause`.
  These are placed in a clearly separate candidate location,
  `THIRD_PARTY_LICENSES/ruby-candidate/`, and are **not** added to the default
  `THIRD_PARTY_LICENSES/manifest.json` or the default bundle. The candidate
  license-text policy lives in a parallel dict in `scripts/check-license-texts.py`
  (`RUBY_CANDIDATE_TEXTS`) consulted only in `--artifact-kind ruby-candidate`
  mode.
- **Reused existing shipped texts:** `MIT`, `UPL-1.0`, `ICU` reuse the
  already-checked-in `THIRD_PARTY_LICENSES/*.txt` files unchanged.
- **EPL-2.0:** becomes a *shipped* requirement in candidate mode (TruffleRuby
  core, internal runtime, resources, annotations, shared). The candidate policy
  ships a copy of the EPL-2.0 text under `THIRD_PARTY_LICENSES/ruby-candidate/`
  with corrected provenance (see finding below).

### EPL-2.0 provenance finding (R4)

The default `EXPECTED_TEXTS["EPL-2.0"]` in `scripts/check-license-texts.py` cites
its provenance as:

> `org.junit.jupiter:junit-jupiter:5.14.1 META-INF/LICENSE.md`

That is **junit** provenance, which is correct for the default artifact (EPL-2.0
appears there only at *test* scope). It is the **wrong provenance for TruffleRuby
redistribution**. In candidate mode EPL-2.0 is a *shipped* license carried by the
`dev.truffleruby:*` artifacts, so the candidate policy cites a TruffleRuby-
appropriate source instead:

> `dev.truffleruby:truffleruby:34.0.1 Eclipse Public License 2.0 (TruffleRuby
> redistribution; corrects junit provenance)`

The default policy is left untouched; the corrected provenance lives only in the
parallel candidate policy. A real Ruby release shape must carry the EPL-2.0 text
with TruffleRuby provenance, not junit provenance.

## Resource / native-surface classification (R1)

The `dev.truffleruby.internal:resources` package carries an embedded TruffleRuby
runtime tree (standard library, bundled gems, native payload). The candidate
SBOM records the denied surface as a **separate** SPDX annotation object on that
package (never appended to `annotations[0].comment`, whose `;`/`=` parser would
corrupt on tokens like `ffi/fiddle`):

```
ruby-denied-surface=rubygems,bundler,openssl,sockets,ffi-fiddle,native-extensions,native-so
```

### Surfaces that ship but must stay denied at runtime

These exist in the resources payload and must remain blocked by TruffleRuby
denial options (per ADR-0027), even though the files ship:

- `rubygems`, `bundler` — package managers (network + arbitrary code).
- `openssl` — native crypto binding surface.
- `sockets` — network access.
- `ffi` / `fiddle` — native FFI surfaces.
- Native extensions and bundled `*.so` payloads (see prune list below).

### Surfaces that are candidates to PRUNE from a future Ruby artifact

If a Ruby artifact is ever built, the following should be pruned from the shipped
resources to reduce size and attack surface, since they are denied anyway:

- `rubygems`, `bundler`
- `openssl`
- `sockets`
- `ffi` / `fiddle`
- native extensions: `bigdecimal`, `debug`, `nkf`, `racc`, `rbs`, `syslog`
- native `*.so` payloads

Pruning decisions are deferred to the Ruby release-shape task (M12-001C); this
inventory only records the classification.

## Vulnerability monitoring (B1, ADR-0027)

All 14 candidate coordinates are `pkg:maven/...` purls. The existing
`maven-components` rule in `docs/security/release-security-state.json` already
matches `purlType=maven` across `shipped`/`build`/`test` scopes, so **all 14 are
already covered** — no redundant overlapping rule was added. A unit test proves a
ruby-candidate SBOM's shipped/build packages have **no monitoring gap**
(`trackingCoverage == coverageRequired`).

Per ADR-0027, TruffleRuby has an **independent release/security cadence** tracked
separately from GraalVM CPU updates. A monitoring SOURCE entry
(`id: truffleruby-releases`,
`url: https://github.com/oracle/truffleruby/releases`) was added to record the
tracking owner for `dev.truffleruby:*`. No candidate vuln check points at the
default release zip or the published SBOM sidecar.

## Tooling gaps closed by M12-004

- License attribution: `scripts/license-report.py` now resolves the candidate
  POM licenses and emits the two new SPDX ids (`BSD-3-Clause`, `BSD-2-Clause`)
  via generated, POM-ordered SPDX expressions.
- Dependency delta: `scripts/check-dep-delta.py` has a `RUBY_CANDIDATE_BASELINE`
  plus a default-baseline forbidden-coordinate guard keyed off the explicit
  14-coordinate set (R5), so `joni`/`jcodings`/`antlr4` (which contain no
  `ruby`/`llvm` substring) cannot silently slip into the default baseline.
- License texts: parallel candidate policy requires `BSD-2-Clause` and
  `BSD-3-Clause` texts and reuses default texts for everything else, without
  mutating the default manifest/bundle.
- Vulnerability monitoring: independent TruffleRuby cadence source recorded;
  coverage of all candidate maven packages proven.
- Distinct SBOM identity: candidate SBOM uses a distinct `documentNamespace`
  (`.../ruby-candidate`) and an explicit
  `support-claim=none; status=blocked-preparation-only` document annotation so it
  can never be conflated with or overwrite the default release SBOM (B4, S1).

## Tooling gaps still remaining

- No Ruby-enabled XCFramework / Native Image artifact exists yet; the candidate
  inventory is metadata-only.
- Resource pruning (which denied surfaces to physically remove) is not yet
  enforced by tooling — deferred to M12-001C release-shape.
- A real Ruby release would need the candidate license texts (and corrected
  EPL-2.0 provenance) promoted into a real packaged bundle once the support gates
  pass — explicitly out of scope here.

## Package reproducibility (R6)

The candidate SBOM is deterministic under `SOURCE_DATE_EPOCH` (it reuses the same
`sort_keys` code path as the default report; verified by running it twice and
diffing — identical). **Artifact-level package reproducibility (the XCFramework
zip) is OUT OF SCOPE** for M12-004 because no Ruby-enabled artifact exists yet.
Candidate artifact-level reproducibility is deferred to the M12-001C
release-shape task (also recorded in `DEBT.md`).
