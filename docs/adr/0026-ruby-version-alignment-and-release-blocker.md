# ADR-0026: Ruby Version Alignment and Release Blocker

Status: Accepted; old-coordinate blocker superseded by ADR-0027 for the
`dev.truffleruby` validation path
Date: 2026-06-08

## Context

M12 needs Ruby in the shipped default Ecritum runtime. M12-001 proved a private
TruffleRuby feasibility probe, but it used `org.graalvm.polyglot:ruby:25.0.0`
while the repo was pinned to GraalVM `25.0.2`.

The team evaluated three paths:

1. Wait for matching Ruby artifacts on the repo GraalVM line.
2. Downgrade the whole repo to GraalVM `25.0.0`.
3. Ship cross-patch skew: current `25.0.2` JS/Python/tooling plus Ruby `25.0.0`.

Update on 2026-06-09:

- Upstream TruffleRuby no longer follows Oracle/GraalVM versioning after
  `25.0.0`.
- Current TruffleRuby Maven coordinates are `dev.truffleruby:truffleruby`.
- `dev.truffleruby:truffleruby:34.0.1` is released and depends on
  `org.graalvm.truffle:truffle-runtime:25.0.2` and
  `org.graalvm.llvm:llvm-native:25.0.2`.
- ADR-0027 accepts that new coordinate line as the next validation path. This
  ADR still rejects the old `org.graalvm` `25.0.0` cross-patch and public
  downgrade options.

Current coordinate evidence:

- Maven metadata for `org.graalvm.polyglot:ruby` reports `25.0.0` as the latest
  and release version.
- `org.graalvm.polyglot:ruby:25.0.2`, `25.0.3`, `25.0.4`, and `25.1.3` are
  absent in local Maven checks.
- `25.0.0` resolves coherently for `polyglot`, `js-community`, `python`,
  `ruby`, and `nativeimage`.
- `25.0.2` resolves for JS, Python, and Native Image, but not Ruby.
- `25.0.3` resolves for JS, Python, and Native Image, but not Ruby.

Local proof for the whole-repo `25.0.0` path:

- `mise install java@graalvm-community-25.0.0` succeeded.
- `native-image --version` reported GraalVM CE `25+37.1`.
- `mvn clean test -Pfull -Dgraalvm.version=25.0.0` passed 66 tests.
- `mvn test -Pruby-probe -Dgraalvm.version=25.0.0
  -Dtest=RubyProbeEvaluatorTest` passed 4 tests.
- Native Image with `native-maven-plugin` `1.1.1` failed on GraalVM `25.0.0`
  unless the plugin metadata repository was disabled, because the metadata
  repository schema is too new for GraalVM `25.0.0`.
- With `<metadataRepository><enabled>false</enabled></metadataRepository>` in a
  temporary POM copy, Ruby probe Native Image passed: 221.42 MB image, 3,558
  resources, 6 foreign downcalls, peak RSS 7.98 GB.
- With the same temporary POM setting, the current full Clojure/JavaScript/Lua/
  Python Native Image passed: 366.40 MB image, 5,215 resources, zero foreign
  downcalls, peak RSS 17.01 GB. Symbol checks and C native eval smoke passed.

Security and release evidence:

- Official GraalVM release notes identify `25.0.1` and `25.0.2` as Oracle CPU
  updates for GraalVM Community Edition 25:
  https://www.graalvm.org/release-notes/JDK_25/
- The same release notes say `25.0.2` fixed the reachability metadata schema and
  included it in the GraalVM distribution.
- The GraalVM release calendar lists later CPU levels after `25.0.0`:
  https://www.graalvm.org/release-calendar/
- Official GraalVM vulnerability advisories list CVEs affecting Oracle GraalVM
  `25.0` and later CPU lines:
  https://www.graalvm.org/vulnerability-advisories/
- OSV queries for the relevant Maven packages at `25.0.0` returned no Maven
  package vulnerabilities, but OSV does not override the official GraalVM CPU
  advisories.

Claude plan review conditionally accepted whole-repo `25.0.0` alignment only if
the CVE/changelog audit and reachability metadata risk were handled before the
ADR. The audit found that the downgrade would intentionally move away from CPU
release lines and require disabling community reachability metadata.

## Decision

Do not ship Ruby by using cross-patch GraalVM skew.

Do not downgrade the public/default Ecritum runtime to GraalVM `25.0.0` for a
Ruby support claim unless a later ADR explicitly accepts the CPU-regression and
reachability-metadata risks with stronger mitigation evidence.

M12 public Ruby support remains blocked until one of these becomes true:

- The `dev.truffleruby` path is validated under ADR-0027 with release, security,
  Native Image, license/SBOM, metrics, and clean-consumer evidence.
- Matching Ruby artifacts become available for a release-safe GraalVM line used
  by Ecritum.
- A release-safe alternative Ruby implementation is designed and proven under a
  separate ADR.
- The user explicitly accepts a non-release, self-use-only experimental Ruby
  build with known CPU-regression risk and no public support claim.

M12 work may continue only on non-support preparation:

- Historical Ruby/LLVM license and resource dry-run inventory using `25.0.0`,
  plus new-coordinate inventory using `dev.truffleruby:truffleruby:34.0.1`
  under ADR-0027.
- Ruby conformance/security test design that does not claim support.
- Alternative Ruby implementation research.
- Upstream tracking for newer Ruby artifacts.

## Consequences

M12-002 remains blocked for public API/ABI implementation. The Ruby probe stays
private and must not appear in the default artifact, Swift API, README support
matrix, release notes, or conformance claims.

Cross-patch skew is rejected for shipped artifacts because it mixes
`polyglot`/Truffle/runtime packages across patch lines and creates unclear
maintenance and security ownership.

The whole-repo `25.0.0` path is technically viable in local proofs, but not
release-accepted. It would require all of the following before reconsideration:

- A security owner explicitly accepts or mitigates the CPU-regression risk from
  leaving the `25.0.1+`/`25.0.2+` CPU line.
- The reachability metadata issue is mitigated by either a compatible metadata
  repository, explicit local metadata, or tests proving no shipped path depends
  on the disabled metadata.
- Release and license tooling is updated from `25.0.2` evidence to the chosen
  GraalVM line.
- JS, Lua, Python, Clojure, Ruby, C ABI, Swift, examples, packaged app,
  conformance, security abuse, size, RSS, startup, dependency delta, SBOM,
  license text, package reproducibility, and clean consumer gates all pass.

The next EM task is to keep M12 blocked for public Ruby support while adding the
ADR-0027 validation tasks that can prove or reject the new `dev.truffleruby`
path without creating a false support signal.

## Verification

Evidence collected for this ADR:

- `curl -fsSL https://repo1.maven.org/maven2/org/graalvm/polyglot/ruby/maven-metadata.xml`
- Maven coordinate checks for `25.0.0`, `25.0.2`, `25.0.3`, `25.0.4`, and
  `25.1.3`.
- `mise install java@graalvm-community-25.0.0`
- `mise x java@graalvm-community-25.0.0 -- native-image --version`
- `mvn clean test -Pfull -Dgraalvm.version=25.0.0`
- `mvn test -Pruby-probe -Dgraalvm.version=25.0.0
  -Dtest=RubyProbeEvaluatorTest`
- Temporary-POM Native Image proof with metadata repository disabled.
- `just check-native-full build/m12-native-25/full-stable`
- C native eval smoke against `build/m12-native-25/full-stable`
- OSV API queries for GraalVM Maven packages at `25.0.0`.
- Official GraalVM release notes, release calendar, and vulnerability
  advisories linked above.
