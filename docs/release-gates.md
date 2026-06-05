# Ecritum Release Gates

This document is the M1 release-gate baseline. It defines the commands a CI job
can run today and the stricter release checks that block publication until
license and distribution work is complete.

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

The M1 release gate is:

```sh
mise exec -- just native
mise exec -- just xcframework
mise exec -- just release-check
```

`just release-check` runs:

- `just test`
- `just check-abi`
- `just check-xcframework`
- `just inspect`
- `just size`
- `just bench-cold-start`
- `just bench-first-eval`
- `just bench-idle-rss`
- `just check-dep-delta`
- `just package-artifact`
- `just license-report`
- `scripts/license-report.py --strict`

`just bench-swift-cold-start` is represented by the M1 budget policy but is not
part of `release-check` yet. Swift host timing is kept out of release-check
because it is a host-example benchmark rather than a release blocker while the C
ABI packaging gates cover the artifact runtime path. First-eval is part of
`release-check` once the eval ABI exists.

The strict license step exits nonzero while shipped licenses remain unknown.
That is intentional: unknown shipped licenses block release publication.

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
artifacts, revocation mechanics, dependency digest locks, SBOM publication,
CVE monitoring, and a vulnerability response process. These requirements are
recorded here as release gates; implementation and final publication policy are
owned by ADR-015 and M7 release tasks.

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

`just size` prints JSON and applies M1 regression budgets:

- artifact directory: 25,000,000 bytes
- artifact warning: above 15,000,000 bytes or above 10% growth from baseline
- public wrapper binary: 262,144 bytes
- private Graal runtime: 20,000,000 bytes

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

`just license-report` is report-only and exits zero. `just license-report-strict`
and `just release-check` exit nonzero and print release blockers to stderr if
any shipped component has `NOASSERTION`, `UNKNOWN`, or missing license data.

For M1, the strict gate blocks on:

- `EcritumRuntime.xcframework`
- GraalVM Native Image embedded runtime code

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

M1 uses rebuildable provenance plus deterministic archive metadata:

- tool versions are pinned in `.mise.toml`
- Maven dependency versions are pinned in `native/pom.xml`
- `just native` copies Native Image outputs into `build/native/macos-arm64`
- `just xcframework` assembles `dist/local/EcritumRuntime.xcframework`
- `just package-artifact` writes
  `dist/release/EcritumRuntime.xcframework.zip`
- archive entries are sorted
- archive timestamps are normalized to `1980-01-01T00:00:00Z`
- macOS metadata files such as `.DS_Store`, `._*`, and `__MACOSX` are excluded
- `just checksum` prints the SwiftPM checksum for the release zip
- `just inspect` records slice, symbol, resource, install-name, and checksum
  metadata

Byte-identical public releases, signing, notarization, dependency digest locks,
and vulnerability response policy are later release-hardening work.

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
