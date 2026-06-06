# Ecritum Release Gates

This document defines the commands a CI job can run today and the stricter
release checks that block publication until size, license, signing,
notarization, and clean-consumer distribution work is complete.

## CI Smoke

The CI-ready smoke path is:

```sh
mise trust
mise exec -- just setup
mise exec -- just native
mise exec -- just xcframework
mise exec -- just test
```

`just test` is artifact-aware. Before a local XCFramework exists, it runs the
Swift scaffold tests and Java unit tests. After `just xcframework`, it runs the
Swift runtime path against `dist/local/EcritumRuntime.xcframework` plus the Java
unit tests.

## Release Gate Commands

The release gate is:

```sh
mise exec -- just native-core
mise exec -- just xcframework-core
mise exec -- just release-check
```

The default release lane is Core. To validate the combined SCI/GraalJS/Lua
candidate as Full, run:

```sh
mise exec -- just native-full
mise exec -- just xcframework-full
mise exec -- just release-check full
```

`release-check` writes lane-specific evidence under `build/release/<lane>` and,
unless overridden, prepares the deterministic zip at
`dist/release/<lane>/EcritumRuntime.xcframework.zip`.
The release lane is selected only by command arguments; ambient
`ECRITUM_RELEASE_LANE` does not make release-check or direct packaging run Full.

`just release-check` runs:

- `just test`
- `just check-abi`
- `just check-xcframework`
- `just inspect`
- `just bench-cold-start`
- `just bench-first-eval`
- `just bench-idle-rss`
- `just check-dep-delta`
- `just package-artifact` with the selected release lane
- `just package-artifact-verify` with the selected release lane
- `just checksum`
- release-mode `swift package describe --type json` with
  `ECRITUM_RELEASE_RUNTIME_REQUIRED=1`
- `just test-release-consumer-smoke` when `ECRITUM_CONSUMER_ARTIFACT_URL` is
  set; otherwise `build/release/clean-consumer.json` records a skipped HTTPS
  artifact gate
- `just sbom`
- `just check-license-texts`
- `just check-license-texts-zip`
- `just check-vulnerability-response`
- `scripts/size-artifact.py --require-artifact` with the selected release lane
- `scripts/license-report.py --strict`

`just bench-swift-cold-start` is represented by the M1 budget policy but is not
part of `release-check` yet. Swift host timing is kept out of release-check
because it is a host-example benchmark rather than a release blocker while the C
ABI packaging gates cover the artifact runtime path. First-eval is part of
`release-check` once the eval ABI exists.

The default Core lane is a SCI/Clojure-only artifact. The combined
SCI/GraalJS/Lua artifact is classified as a Full candidate and must be selected
explicitly with `full`; ambient `ECRITUM_RELEASE_LANE` does not promote it.
After Core or Full lane size gates pass, the strict license step remains a
publication blocker until the project owner chooses and commits a top-level
Ecritum license.

SwiftPM requires remote binary target URLs to use `https`. Local `http://` and
`file://` URLs are not accepted as release proof. Self-signed loopback HTTPS
with a locally trusted certificate is development evidence only; M7-002 release
acceptance requires a real hosted HTTPS artifact URL and matching checksum. To
run the clean-consumer release gate, publish the lane-specific zip such as
`dist/release/full/EcritumRuntime.xcframework.zip` to an HTTPS URL, then run:

```sh
export ECRITUM_CONSUMER_ARTIFACT_URL=https://.../EcritumRuntime.xcframework.zip
export ECRITUM_CONSUMER_ARTIFACT_CHECKSUM=$(cat dist/release/full/EcritumRuntime.xcframework.zip.checksum)
mise exec -- just test-release-consumer-smoke \
  "$ECRITUM_CONSUMER_ARTIFACT_URL" \
  "$ECRITUM_CONSUMER_ARTIFACT_CHECKSUM" \
  dist/release/full/EcritumRuntime.xcframework.zip
```

The clean-consumer smoke creates a temporary SwiftPM executable package outside
the repo, forces `ECRITUM_RELEASE_RUNTIME_REQUIRED=1`, verifies Ecritum's
release-mode manifest selects the remote `EcritumRuntime` binary target, rejects
stale local zip metadata, builds with an isolated SwiftPM HOME/TMPDIR/build
directory, confirms SwiftPM downloaded `EcritumRuntime.framework` into the build
directory instead of using `dist/local` by inspecting `workspace-state.json`,
runs the consumer executable with an empty `PATH`, and checks linked Mach-O paths
for GraalVM/JDK/native-build leaks.

The M2.5 security baseline adds these CI gates:

- `just test-security-static`
- `just test-security-abuse`
- `just test-security-fuzz`

`test-security-static` blocks forbidden Polyglot, Java interop, native access,
and Native Image metadata patterns outside named negative fixtures.
`test-security-abuse` records the abuse matrix for ambient filesystem, network,
process, environment, reflection, class loading, native loading, unrestricted
Java lookup, raw host access, raw C handle access, and classpath mutation.
`test-security-fuzz` is the current parser-abuse equivalent: it maps available C
config, lifecycle, and host-registration coverage and records blocked eval,
source, value, error, and callback parser surfaces until those public APIs exist.

Release publication also requires a hardened runtime plan, signed/notarized
artifacts, and dependency digest locks. ADR-015 owns SBOM publication, CVE
tracking, vulnerability response, and revocation policy. The offline gate is
`just check-vulnerability-response`, which verifies SPDX SBOM shape,
package-url identity coverage, monitoring-source coverage, advisory blockers,
accepted-risk expiry, and revoked artifact checksums. Live OSV/NVD/CISA/vendor
queries are scheduled release-operations work and are intentionally not part of
the deterministic local `release-check` yet.

## ABI Gate

`docs/abi/ecritum-c-abi.json` is the checked M1 ABI manifest. `just check-abi`
verifies:

- public status constants in `Sources/CEcritum/include/ecritum.h`
- matching Java status constants in `native/src/main/java/ecritum/EcritumStatus.java`
- public `ecritum_version` declaration
- packaged `_ecritum_version` export when the local XCFramework exists
- absence of private Graal symbols from the public wrapper binary

`ECRITUM-DEBT-0002` still tracks the duplicated C/Java status-code source of
truth. The ABI gate prevents drift until M2 replaces that duplication.

## Inspection And Size

`just inspect` prints JSON with artifact paths, symbols, linked dylibs,
install names, bundled resources, code-signing status, architectures, minimum
macOS version, checksums, and embedded runtime list.

`just size dist/core/EcritumRuntime.xcframework core` prints JSON and applies
Core regression budgets:

- artifact directory: 35,000,000 bytes
- artifact warning: above 33,000,000 bytes or above 10% growth from baseline
- public wrapper binary: 262,144 bytes
- private Graal runtime: 33,000,000 bytes

`just size dist/full/EcritumRuntime.xcframework full` applies Full candidate
budgets:

- artifact directory: 200,000,000 bytes
- artifact warning: above 175,000,000 bytes or above 10% growth from Full
  baseline
- public wrapper binary: 262,144 bytes
- private Graal runtime: 190,000,000 bytes

See [performance-and-artifact-budgets.md](performance-and-artifact-budgets.md)
and ADR-018 for startup, first-eval, idle-RSS, dependency-delta, and Core/Full
artifact gates.

## SBOM And License Policy

The chosen SBOM baseline is SPDX 2.3 JSON. `just license-report` emits an SPDX
document with standard document and package annotations for:

- shipped components
- build-only components
- test-only components
- shipped-license blockers

`just sbom` writes the same SPDX 2.3 JSON to a release sidecar path. SPDX
packages include package-url external references so downstream CVE tooling can
map Maven dependencies, the first-party XCFramework, and the GraalVM Native
Image embedded runtime identity. `just check-vulnerability-response` validates
that every covered package has purl identity and monitoring coverage in
`docs/security/release-security-state.json`.

`just license-report` is report-only and exits zero. `just license-report-strict`
and `just release-check` exit nonzero and print release blockers to stderr if
any shipped component has `NOASSERTION`, `UNKNOWN`, or missing license data.
`just third-party-notices` regenerates the checked-in `THIRD_PARTY_NOTICES.md`;
`release-check` verifies the generated notices match the checked-in file. POMs
with multiple declared licenses are represented as conservative combined SPDX
expressions unless a runtime ADR accepts a narrower upstream interpretation.
`just check-dep-delta` compares scope, component name, exact version, and SPDX
expression against the reviewed release baseline.

ADR-011 resolves GraalVM Community 25.0.2 Native Image output as
`GPL-2.0-only WITH Classpath-exception-2.0` when the local GraalVM CE
`LICENSE_NATIVEIMAGE.txt` evidence and `native-image --version` output match the
recorded policy. The strict gate still blocks on `EcritumRuntime.xcframework`
until the project owner chooses and commits a top-level Ecritum license.

`THIRD_PARTY_NOTICES.md` is an inventory index, not a full license-text bundle.
M7-004 adds `THIRD_PARTY_LICENSES/` as the checked-in full-text bundle and
`just check-license-texts` / `just check-license-texts-zip` as the artifact and
release-zip gates. `just xcframework` copies the bundle into
`EcritumRuntime.framework/Resources/Licenses` before codesigning, and
`release-check` verifies both the XCFramework artifact and deterministic zip
against the SPDX license report generated in the same release run.
POM-only resolver artifacts such as `org.graalvm.polyglot:js-community` and
Maven build plugins are excluded from this shipped-runtime full-text gate because
they are not redistributed in the SwiftPM binary target.

## Vulnerability Response And Revocation

ADR-015 defines the release-security policy.
`docs/security/release-security-state.json` is the machine-readable advisory and
revocation state, while `docs/security/vulnerability-response.md` is the
operator runbook.

The vulnerability response gate fails when:

- the SBOM is not SPDX 2.3
- a shipped, build, or test component lacks purl identity
- a covered component has no monitoring rule
- a high, critical, or known-exploited open advisory affects the current SBOM
- an accepted-risk advisory lacks an ADR, rationale, or unexpired acceptance
  window
- a revocation entry is malformed
- the current release zip checksum or URL is listed as revoked

Published artifact bytes are immutable. Do not replace a zip at an existing
SwiftPM URL. If an artifact is unsafe, publish a revocation entry with the exact
URL, zip SHA-256, SwiftPM checksum, SBOM SHA-256, date, and reason, then ship a
fixed artifact at a new URL/checksum.

The current Maven SDK inputs `org.graalvm.sdk:nativeimage` and
`org.graalvm.sdk:word` are inventoried separately as build-time inputs. JUnit is
inventoried as test-only.

M3-002B adds embedded SCI Clojure eval. These Maven runtime dependencies are
compiled into the Native Image and are therefore inventoried as shipped
components:

- `org.babashka:sci:0.12.51` from Clojars, EPL-1.0.
- `org.clojure:clojure:1.10.3`, EPL-1.0.
- `org.clojure:spec.alpha:0.2.194`, EPL-1.0.
- `org.clojure:core.specs.alpha:0.2.56`, EPL-1.0.
- `borkdude:edamame:1.5.37` from Clojars, EPL-1.0.
- `org.clojure:tools.reader:1.5.2`, EPL-1.0.
- `org.babashka:sci.impl.types:0.0.2` from Clojars, manually accepted as
  EPL-1.0 based on Clojars/cljdoc metadata for the SCI project because the
  artifact POM omits a license element.
- `borkdude:graal.locking:0.0.2` from Clojars, EPL-1.0.

The accepted M3-002B dependency tree from
`mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml dependency:tree
-Dscope=runtime` is:

```text
dev.ecritum:ecritum-native:jar:0.1.0-SNAPSHOT
+- org.graalvm.sdk:nativeimage:jar:25.0.2:compile
|  \- org.graalvm.sdk:word:jar:25.0.2:compile
\- org.babashka:sci:jar:0.12.51:compile
   +- org.clojure:clojure:jar:1.10.3:compile
   |  +- org.clojure:spec.alpha:jar:0.2.194:compile
   |  \- org.clojure:core.specs.alpha:jar:0.2.56:compile
   +- borkdude:edamame:jar:1.5.37:compile
   |  \- org.clojure:tools.reader:jar:1.5.2:compile
   +- org.babashka:sci.impl.types:jar:0.0.2:compile
   \- borkdude:graal.locking:jar:0.0.2:compile
```

M5-001 adds experimental Lua through LuaJ. The accepted shipped dependency is:

- `org.luaj:luaj-jme:3.0.1`, MIT.

The LuaJ JME POM has no transitive runtime dependencies. The accepted M5-001
dependency-tree delta is:

```text
+- org.luaj:luaj-jme:jar:3.0.1:compile
```

Lua remains an experimental measured candidate, not a release-ready Core
runtime. M5-001 verification proved `CoroutineLib` is omitted and
`string.dump`/binary chunks are denied. Core promotion is blocked until
ADR-018/Core-Full classification is resolved with Lua size/startup/RSS/
first-eval data and a memory-limiting plan exists for untrusted Lua workloads.

M6-001 gates Python through GraalPy. ADR-008 rejects GraalPy inclusion in the
default Core artifact for v0 and keeps Python as a Full-artifact candidate until
a later spike proves Native Image shared-library packaging, standard-library
resource layout, conformance/security behavior, size/startup/RSS/first-eval
impact, license inventory, and dependency delta. Native wheels, C extensions,
runtime `pip`, direct Java imports, raw Polyglot bindings, `ctypes`, `cffi`,
subprocess, raw sockets, environment access, and direct filesystem access remain
non-goals unless a later ADR explicitly accepts a narrow facade.

M6-002 gates Ruby through TruffleRuby. ADR-009 rejects TruffleRuby inclusion in
the default Core artifact for v0 and keeps Ruby as a Full-artifact candidate
until a later spike proves matching GraalVM/Maven coordinates or an accepted
version-skew policy, Native Image shared-library packaging, runtime-resource
layout, conformance/security behavior, size/startup/RSS/first-eval impact,
license inventory, dependency delta, reproducible packaging, clean-consumer
behavior, and macOS slice policy. RubyGems/Bundler installation, native gems, C
extensions, NFI/FFI, direct Java imports, raw Polyglot bindings, subprocess, raw
sockets, environment access, and direct filesystem access remain non-goals
unless a later ADR explicitly accepts a narrow facade.

## Reproducibility

M7 uses rebuildable provenance plus deterministic archive metadata:

- tool versions are pinned in `.mise.toml`
- Maven dependency versions are pinned in `native/pom.xml`
- `just native` copies Native Image outputs into `build/native/macos-arm64`
- `just xcframework` assembles `dist/local/EcritumRuntime.xcframework`
- local artifacts are ad-hoc signed by signing the nested private dylib first
  and then the framework bundle
- `release-check` invokes `just package-artifact` to write
  `dist/release/<lane>/EcritumRuntime.xcframework.zip`
- `release-check` also writes
  `dist/release/<lane>/EcritumRuntime.xcframework.zip.json` and
  `dist/release/<lane>/EcritumRuntime.xcframework.zip.checksum`
- archive entries are sorted
- archive timestamps are normalized to `1980-01-01T00:00:00Z`
- macOS metadata files such as `.DS_Store`, `._*`, and `__MACOSX` are excluded
- `just package-artifact-verify` packages the same input twice and compares
  bytes, SHA-256, SwiftPM checksum, sidecar checksums, entry order, timestamps,
  compression type, and metadata skips
- `just checksum` prints the SwiftPM checksum for the release zip
- `just inspect` records slice, symbol, resource, install-name, and checksum
  metadata
- `just test-release-consumer-smoke` records HTTPS binary-target consumer
  evidence when a real HTTPS artifact URL is supplied

Public release signing requires a Developer ID identity, hardened runtime,
notarization with `notarytool`, and retained notarization evidence. Hosted
SwiftPM URL resolution remains M7-002. Dependency digest locks, SBOM/CVE
policy, and vulnerability response policy are owned by ADR-011 and ADR-015.

## Smoke Test Representation

The clean-machine dynamic-loading smoke is represented by
`scripts/check-xcframework.sh`: it compiles a temporary C program, `dlopen`s the
framework binary, resolves `ecritum_version` with `dlsym`, checks the version
string, and verifies negative buffer paths.

The persistent C host example is represented in PROJECT.org as M1-006. It will
cover direct include/link behavior with `ecritum.h` outside SwiftPM.

Java runtime unit tests are represented by `native/src/test/java/ecritum/`.
Today they cover the Native Image entrypoint version string and buffer/status
rules.
