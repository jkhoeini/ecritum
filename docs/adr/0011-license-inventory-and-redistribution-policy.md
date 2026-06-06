# ADR-011: License Inventory And Redistribution Policy

Status: Accepted

Reviewers: Release, Technical Debt, Security, GraalVM Runtime. Claude CLI plan
feedback was incorporated; final diff-review attempts timed out with no output.
M8-003 MIT plan consult was invoked directly with `claude -p` and timed out
with no output.

## Context

Ecritum distributes a SwiftPM binary target containing a native shared library
produced by GraalVM Native Image. The artifact also embeds Java dependencies
used by SCI Clojure, GraalJS, LuaJ, and the Ecritum wrapper code. M1 added an
SPDX 2.3 inventory and strict release blocker for unknown shipped licenses, but
M7 needs third-party notices and a clearer policy for first-party code,
Native Image output, build-only inputs, and future runtime additions.

The local build tool is GraalVM Community 25.0.2:

```text
native-image 25.0.2 2026-01-20
GraalVM Runtime Environment GraalVM CE 25.0.2+10.1
Substrate VM GraalVM CE 25.0.2+10.1
```

That distribution includes `LICENSE_NATIVEIMAGE.txt` and
`THIRD_PARTY_LICENSE.txt`. The official GraalVM FAQ says GraalVM Community
Edition is distributed under GPLv2 with the Classpath Exception and recommends
checking individual component licenses:
https://www.graalvm.org/faq/

Older Native Image FAQ documentation is historical context, not decisive
evidence for this ADR. It discusses Native Image licensing but its artifact
answer points at Enterprise/OTN wording, so this decision relies on the local
GraalVM Community `LICENSE_NATIVEIMAGE.txt` evidence plus the current GraalVM
FAQ:
https://www.graalvm.org/22.2/reference-manual/native-image/FAQ/

Before M8-003 the repository had no top-level `LICENSE`, `NOTICE`, or
equivalent first-party licensing document, so the release tooling kept
Ecritum's first-party package as `NOASSERTION`. In M8-003 the project owner
chose MIT for first-party Ecritum code and accepted
`Copyright (c) 2026 Ecritum contributors` as the copyright line.

The local `LICENSE_NATIVEIMAGE.txt` evidence used for this decision has SHA-256
`11a8fe0c63dcff8bd8674b89a5895dfbcf5f7e5453cf0a33566c4b3fb64e404c`.

## Decision

`scripts/license-report.py` remains the source of truth for the generated SPDX
inventory and generated `THIRD_PARTY_NOTICES.md`.

Package scopes are:

- `shipped`: code or runtime material included in the XCFramework artifact or
  Native Image output.
- `build`: tools and SDK inputs required to build the artifact but not shipped
  as separate runtime components.
- `test`: test-only dependencies.

Every shipped package must have a known SPDX license expression or the strict
release gate fails. Build and test packages are inventoried but do not block a
runtime release unless a future policy makes them publication inputs.

The first-party Ecritum code is licensed under MIT through the top-level
`LICENSE` file. The first-party `EcritumRuntime.xcframework` SPDX package is
reported as:

- package: `EcritumRuntime.xcframework`
- version: `0.1.0-dev`
- scope: `shipped`
- license: `MIT`
- license source: `LICENSE`
- copyright: `Copyright (c) 2026 Ecritum contributors`

This first-party license entry does not relicense GraalVM, SCI, Clojure,
GraalJS, LuaJ, ICU, UPL, EPL, GPL+Classpath Exception, or other third-party
runtime material. Those components remain separately inventoried with their own
SPDX expressions, license sources, full-text packaging gates, and notice
obligations.

The current GraalVM Community Native Image output is inventoried as:

- package: `GraalVM Native Image embedded runtime code`
- version: `25.0.2`
- scope: `shipped`
- license: `GPL-2.0-only WITH Classpath-exception-2.0`

This classification is accepted only for GraalVM Community 25.0.2 builds that
provide the local `LICENSE_NATIVEIMAGE.txt` evidence and match the recorded
`native-image --version` output. Switching to Oracle GraalVM under GFTC,
Mandrel, another downstream GraalVM distribution, or a different major licensing
line requires updating this ADR and the inventory before release.

Maven dependency POM license metadata is accepted for Maven artifacts when all
of these are true:

- the exact version is pinned in `native/pom.xml` or an accepted runtime ADR;
- the package is present in `scripts/license-report.py`;
- the generated SPDX expression is not `NOASSERTION`;
- `just check-dep-delta` has no unreviewed added, removed, version-changed, or
  SPDX-changed component.

When a POM declares multiple licenses, the inventory records the package with a
combined `AND` SPDX expression unless a later ADR or upstream license file proves
the artifact is dual-licensed and may use `OR`. This keeps notice obligations
conservative for GraalJS packages that declare both UPL and MIT.

`org.graalvm.sdk:nativeimage` and `org.graalvm.sdk:word` stay build-scoped even
though Maven reports them as compile dependencies, because they are Native Image
SDK/build API inputs rather than separate runtime artifacts in the distributed
XCFramework. Release inspection and clean-consumer gates must keep proving that
no Maven JARs, JDK installation, or build-machine GraalVM paths are distributed
as separate consumer artifacts.

The `org.graalvm.polyglot:js-community` dependency in `native/pom.xml` is a
POM-only resolver for GraalJS runtime dependencies. It is not inventoried as a
separate shipped artifact or full-text obligation; the resolved GraalJS runtime
artifacts are inventoried individually.

Maven build plugins and their transitive dependency graphs are explicitly
excluded from the shipped-runtime notice and full-text bundle because they are
not redistributed in the SwiftPM binary target or the packaged XCFramework.
They remain build-environment dependencies. If Ecritum later vendors, mirrors,
publishes, or redistributes build tools or plugin artifacts, a new build-tool
license inventory task and ADR update must precede that distribution.

Manual overrides are allowed only with a comment in `scripts/license-report.py`
or an ADR note that names the upstream metadata source and exact version. The
current manual override is `org.babashka:sci.impl.types:0.0.2` as EPL-1.0,
already recorded in the SCI task notes.

`THIRD_PARTY_NOTICES.md` is generated deterministically, checked in, and
verified during `release-check`. It lists release blockers plus shipped,
build-only, and test-only components with exact versions, SPDX expressions, and
source URLs. It intentionally does not include full license text.

`THIRD_PARTY_LICENSES/` is the checked-in full-text bundle for shipped runtime
license obligations. M7-004 adds release gates that verify this bundle, the
packaged XCFramework resources, and the release zip against the SPDX report.
The bundle's generic `MIT.txt` satisfies third-party MIT obligations only. When
the first-party Ecritum package reports `license-source=LICENSE`,
`scripts/build-xcframework.sh` copies the real top-level license to
`Resources/Licenses/Ecritum-LICENSE.txt`, and `scripts/check-license-texts.py`
verifies that copy by SHA-256 in both the XCFramework and release zip.

ADR-015 still owns vulnerability response, SBOM/CVE tracking, revocation, and
public artifact withdrawal policy. ADR-011 only owns license inventory and
notice generation.

## Consequences

Strict license release checks now fail only for genuinely unresolved shipped
licenses or stale first-party license evidence. For the current tree, the
first-party Ecritum package and GraalVM Community Native Image package both have
accepted license evidence.

The first-party license blocker is resolved. Public release can still be blocked
by signing, notarization, hosted SwiftPM consumer evidence, vulnerability
response, size, or future unresolved shipped-license changes.

Future runtime additions cannot hide behind the aggregate Native Image output.
Each shipped Maven/runtime component must appear in the inventory and
dependency-delta baseline.

## Alternatives Considered

- Keep GraalVM Native Image output as `NOASSERTION`.
  This is safer than an unsupported license claim, but it ignores local
  GraalVM CE 25.0.2 license evidence and official GraalVM Community licensing
  documentation.
- Classify GraalVM Native Image output as `UPL-1.0`.
  Rejected. Several GraalVM Maven components are UPL-1.0, but that does not
  establish the license for the Native Image generated runtime output.
- Apache-2.0 or BSD-3-Clause for first-party Ecritum code.
  Rejected by owner decision in M8-003. MIT is the smallest permissive option
  for the current SwiftPM/C ABI runtime distribution model.
- Generate notices only into `build/release`.
  Rejected. M7 requires a checked-in notice artifact so release reviews can diff
  notice changes alongside dependency and license changes.

## Verification Plan

- `python3 -m py_compile scripts/license-report.py`
- `mise exec -- just license-report`
- `mise exec -- just license-report-strict core`
- `mise exec -- just license-report-strict full`
- focused tests proving missing or stale first-party `LICENSE` fails strict mode
- `mise exec -- just third-party-notices`
- `mise exec -- just test-release-tools`
- `mise exec -- just check-dep-delta`
- `mise exec -- just release-check`, or recorded expected failures for signing,
  notarization, hosted clean-consumer evidence, size, or unrelated release
  operations
