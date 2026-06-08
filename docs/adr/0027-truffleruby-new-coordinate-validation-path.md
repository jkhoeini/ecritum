# ADR-0027: TruffleRuby New Coordinate Validation Path

Status: Accepted
Date: 2026-06-09

## Context

ADR-0026 blocked public Ruby support because Ecritum is pinned to GraalVM
`25.0.2`, while the old Oracle/GraalVM Maven coordinates for Ruby only publish
`org.graalvm.polyglot:ruby:25.0.0`. The team rejected both cross-patch shipped
artifacts and a public downgrade to GraalVM `25.0.0`.

New upstream evidence changes the available path:

- TruffleRuby is no longer released by Oracle on the GraalVM versioning
  schedule.
- The last GraalVM/Truffle-versioned TruffleRuby release is `25.0.0`.
- Current TruffleRuby Maven coordinates are `dev.truffleruby:truffleruby`.
- Maven Central lists `dev.truffleruby:truffleruby:34.0.1` as the latest
  release.
- The `34.0.1` POM depends on `org.graalvm.truffle:truffle-runtime:25.0.2` and
  `org.graalvm.llvm:llvm-native:25.0.2`.
- The upstream compatibility table says TruffleRuby `34.0.0` uses Truffle
  `25.0.2` and is compatible with GraalVM `21.0.x` and `25.0.x`.

This is not the same as the rejected `25.0.0` cross-patch skew. It is a new
release line with Truffle and LLVM dependencies aligned to Ecritum's current
GraalVM line.

## Decision

Use `dev.truffleruby:truffleruby:34.0.1` as the next candidate path for M12
validation.

This ADR does not accept public Ruby support and does not unblock M12-002 by
itself. It only replaces the stale "wait for `org.graalvm.polyglot:ruby:25.0.2`"
assumption with a new validation path.

M12-002 remains blocked until M12-001B and the release/security inventory gates
prove all of the following:

- The new coordinates resolve locally through the repo's Maven settings.
- `Context.newBuilder("ruby")` works with the repo's Polyglot API line.
- JavaScript, Python, Ruby, Truffle, Polyglot, LLVM, and Native Image artifacts
  coexist on a single `25.0.2`-aligned dependency graph.
- Native Image can build the Ruby probe without disabling release-required
  reachability metadata.
- Ruby denial options still block RubyGems, Bundler, native extensions, NFI/FFI,
  native access, Java access, raw Polyglot access, process, network,
  environment, and unrestricted filesystem use.
- TruffleRuby internal artifacts, runtime resources, bundled gems, LLVM payload,
  license files, SBOM entries, and vulnerability ownership are inventoried.
- Size, cold start, first eval, idle RSS, dependency delta, license/SBOM,
  package reproducibility, and clean SwiftPM consumer gates pass before any
  support claim.

ADR-0026 remains accepted for the old choices: do not ship `org.graalvm` Ruby
`25.0.0` cross-patch skew, and do not downgrade the public Ecritum runtime to
GraalVM `25.0.0` without a separate explicit security/release decision.

## Consequences

M12 changes from "blocked until old `org.graalvm` artifacts appear" to "blocked
until the `dev.truffleruby` path is proven in Ecritum's actual Native
Image/XCFramework release shape."

The existing `ruby-probe` code remains private spike infrastructure. It must not
become a public API, Swift language support claim, README support matrix entry,
release-note claim, or default artifact dependency until the new-coordinate
validation path passes.

`dev.truffleruby.internal:*` artifacts are treated as shipped implementation
dependencies, even if upstream labels them internal. Release tooling must
inventory and monitor them before product support is accepted.

TruffleRuby now has an independent release and security cadence. Ecritum must
track TruffleRuby updates separately from GraalVM CPU updates.

## Alternatives Considered

- Keep waiting for `org.graalvm.polyglot:ruby:25.0.2`.
  Rejected because upstream states the last GraalVM-versioned TruffleRuby
  release was `25.0.0`; the current path moved to `dev.truffleruby`.
- Treat `dev.truffleruby:truffleruby:34.0.1` as immediate support.
  Rejected because Ecritum has not yet proven Polyglot registration, Native
  Image closure, resource lookup, denial options, license/SBOM, size, metrics,
  or clean-consumer packaging.
- Keep the old `25.0.0` probe and accept cross-patch skew.
  Rejected by ADR-0026 for shipped artifacts.
- Switch to another Ruby implementation immediately.
  Deferred. M12-005 remains as a fallback research path if the new
  TruffleRuby coordinates fail release validation.

## Verification Plan

Required M12-001B evidence:

- `curl -fsSL https://repo1.maven.org/maven2/dev/truffleruby/truffleruby/maven-metadata.xml`
- `curl -fsSL https://repo1.maven.org/maven2/dev/truffleruby/truffleruby/34.0.1/truffleruby-34.0.1.pom`
- `mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml dependency:get -Dartifact=dev.truffleruby:truffleruby:34.0.1:pom -Dtransitive=true`
- Maven dependency tree for a new isolated `ruby-probe` path using
  `dev.truffleruby:truffleruby:34.0.1` and repo GraalVM `25.0.2`.
- Java probe proving `Context.newBuilder("ruby")` can evaluate Ruby.
- Native Image Ruby probe build using the same metadata policy as release
  builds.
- C native Ruby probe smoke.
- Static security and Ruby abuse denials.
- License/SBOM/resource inventory for `dev.truffleruby`, internal runtime and
  resources, LLVM, bundled gems, and license-like files.
- Dependency delta, size, first eval, cold start, idle RSS, package
  reproducibility, and clean SwiftPM consumer checks before support claim.

## Reviewers

- Engineering Manager: accepted conditional validation path.
- GraalVM Runtime: add validation task; block implementation until new
  coordinates are proven in Ecritum.
- Architecture/Security: amend the old blocker; require denial and resource
  decisions before implementation.
- Release/TDD: sequence coordinate proof, license/SBOM/resource inventory, then
  Native Image probe before public API work.
- Claude CLI plan review: no structural blocker; required local Maven proof,
  Polyglot registration proof, license check, independent security-cadence
  tracking, and conditional wording.
