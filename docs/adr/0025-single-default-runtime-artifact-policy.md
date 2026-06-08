# ADR-025: Single Default Runtime Artifact Policy

Status: Accepted for M9-001.

Supersedes the next-release artifact policy portions of ADR-008, ADR-009,
ADR-010, ADR-018, and ADR-023. Those ADRs remain historical records for the
milestones that produced them.

## Context

Ecritum v0.1.0 shipped a Community Core release. The repository also had Full
lane tooling for the larger SCI/GraalJS/Lua runtime. The project owner has
decided that the public Core/Full split is no longer worth carrying for the next
release: even the smaller artifact is large enough to require the same binary
framework distribution model, and the desired product is the complete embedded
polyglot runtime.

The accepted next-release requirement is:

> A Swift app can depend on Ecritum through normal SwiftPM, build without
> Ecritum-specific environment variables, and ship one `.app` bundle containing
> the app plus `EcritumRuntime.framework`. That framework contains everything
> needed to run Clojure, JavaScript, Lua, Python, and Ruby scripts without
> external GraalVM, JDK, Node, Python, Ruby, or Clojure installs.

Status as of M12: Python has local default-artifact support, but Ruby is not
currently supported in public artifacts. Ruby remains blocked by release
inventory/tooling, release-shape packaging proof, old-probe cleanup, public
API work, and security gates.

The same release intentionally keeps Python and Ruby package support minimal.
The project does not want to support `pip`, RubyGems, third-party package
directories, package downloads, package installs, native extensions, native
wheels, C extensions, `ctypes`, `cffi`, FFI/NFI, or mutable package caches in
this release.

The v0.1.0 release also proved a distribution gap: a clean SwiftPM consumer can
build from the tag without Ecritum environment variables, but the runtime is not
available because `Package.swift` only uses a remote binary target when
`ECRITUM_RUNTIME_URL` and `ECRITUM_RUNTIME_CHECKSUM` are set.

## Decision

The next Community release has one public runtime artifact:

- `EcritumRuntime.xcframework.zip` as the SwiftPM binary-target archive.
- `EcritumRuntime.framework` embedded in packaged macOS apps.
- No public Core and Full release artifacts.

The packaged app layout remains dynamic-library based. A host app ships one
`.app` bundle containing its executable plus
`Contents/Frameworks/EcritumRuntime.framework`. All Ecritum language runtimes,
native libraries, resource inventories, and standard/runtime resources needed by
the supported languages must live inside `EcritumRuntime.framework`; users must
not install external GraalVM, JDK, Node, Python, Ruby, Clojure, or Lua runtimes.

The public ABI remains language-neutral and C-compatible. Adding Python and Ruby
must not add language-specific public C symbols. Clojure, JavaScript, Lua,
Python, and Ruby all route through the existing eval/job/value/error/lifecycle
and host-capability ABI. Private Native Image entrypoints are allowed as
implementation details behind the C wrapper.

Internal build profiles, directory names, or historical lanes may remain only
when they are implementation details. They must not appear as product choices in
new user-facing docs, release notes, package manifests, or support claims.

The release target is five-language support: Clojure/SCI, GraalJS, LuaJ,
GraalPy, and TruffleRuby. No language is supported until it passes strict
conformance and strict abuse gates with zero required pending cases. A release
with fewer than five languages is not a fallback for this project; it requires a
new ADR and explicit user approval.

Python support is runtime-and-stdlib-only for the next release. Ecritum must not
enable `pip`, third-party Python package directories, package downloads,
package installs, native extensions, native wheels, `ctypes`, `cffi`, FFI/NFI,
mutable package caches, direct Java access, raw Polyglot access, subprocess,
raw network, unrestricted filesystem, or environment access.

Ruby support is runtime-and-standard-resource-only for the next release. Ecritum
must not enable RubyGems installation, Bundler installation, runtime gem
installation, native gems, C extensions, FFI/NFI, mutable gem/cache roots,
direct Java access, raw Polyglot access, subprocess, raw network, unrestricted
filesystem, or environment access.

Lua is promoted from a measured local smoke path to a default-artifact candidate
only under the same release rule: strict conformance, strict abuse, metrics,
license inventory, resource-limit evidence, clean consumer, and packaged app
gates must pass before docs claim support.

`Package.swift` must support these resolution modes, in priority order:

1. Local contributor artifact at `dist/local/EcritumRuntime.xcframework`.
2. Explicit release-staging override from `ECRITUM_RUNTIME_URL` and
   `ECRITUM_RUNTIME_CHECKSUM`, which must be provided together.
3. Checked-in hosted GitHub Releases URL and SwiftPM checksum for the current
   public default artifact.
4. Explicit scaffold/no-runtime mode only for tests that intentionally validate
   missing-artifact behavior.

`Package.swift` must not use `native/pom.xml` or other repository source files
as a contributor sentinel, because SwiftPM consumers clone the source repository
and would also have those files.

When `ECRITUM_RELEASE_RUNTIME_REQUIRED=1` is set, manifest evaluation must force
remote binary-target resolution and ignore local `dist/local` artifacts. This
keeps release validation from accidentally testing a local artifact while the
published package points at a hosted URL.

M10 must choose the next version, tag naming, staged artifact URL, and final
hosted URL replacement rules before changing the checked-in default binary
target URL. M14 must confirm that the final tag and uploaded asset match the
committed URL/checksum before publication.

Release staging order is: choose the next tag, build and package the artifact,
compute the SwiftPM checksum, update the checked-in URL/checksum, tag that
commit, upload the exact assets, then run the hosted no-env clean SwiftPM
consumer from the tag.

The performance policy changes from Core-vs-Full deltas to single-default
artifact measurements. The release candidate must record zip size, unzipped
framework size, app bundle delta, cold start, first eval per language, idle RSS,
dependency delta, license/SBOM inventory, and resource inventory. README and
release notes must include those measured reference numbers.

Default deny remains the security baseline. Filesystem, network, process,
environment, direct Java, raw Polyglot, reflection/class loading, native library
loading, native access, host access, and package/native-extension behavior are
available only through explicit Ecritum capabilities and reviewed facades.

## Consequences

The consumer story becomes simpler: add Ecritum through SwiftPM and ship one app
bundle containing the app and `EcritumRuntime.framework`.

The artifact will be larger. That is accepted for the next release, but the
release cannot hide the cost. Metrics are product requirements, not polish.

The SwiftPM manifest becomes part of release preparation. A public tag must
resolve a hosted `.binaryTarget(url:checksum:)` by default for normal consumers.
Release staging may still use environment overrides, but the published package
must not require them.

Python and Ruby package ecosystems are outside this release. Future support for
host-approved pure package roots, vendored packages, or package managers needs a
separate ADR, resource model, reproducibility model, and abuse suite.

M11 and M12 are feasibility-gated. If GraalPy or TruffleRuby cannot be made to
work in the framework shape, the milestone is blocked until an ADR and user
decision changes the implementation path or release scope.

## Alternatives Considered

- Keep Core and Full as public artifacts. Rejected because the next product goal
  is the complete embedded runtime and both artifacts impose the same framework
  distribution burden.
- Ship Core by default and make Full opt-in. Rejected for the next release
  because it preserves the consumer confusion this milestone is meant to remove.
- Ship with four languages if one runtime blocks. Rejected for this project
  unless the user explicitly changes the five-language requirement through a new
  ADR.
- Support `pip`, RubyGems, third-party packages, or native extensions now.
  Rejected because package roots, native code, cache writes, reproducibility,
  licensing, and security denials need separate design and tests.
- Use sidecar language runtimes or require users to install language toolchains.
  Rejected because Ecritum's product contract is a prebuilt SwiftPM artifact and
  stable C ABI with no external language runtime installs for consumers.

## Verification Plan

For this ADR:

- `mise exec -- just plan-check`
- `mise exec -- just test`
- `mise exec -- just release-check-community`
- Support-claim scan for stale default Core/Full language in README, docs,
  release scripts, tests, and PROJECT.org.
- Claude review on the ADR diff, or timeout recorded.
- Persona review from Architecture, Release, Security, Tests/TDD, Unix, Clean
  Code, and Technical Debt.

For the implementation milestones:

- M10: clean no-env SwiftPM consumer from a tag resolves the hosted artifact and
  runs Clojure, JavaScript, and Lua.
- M11: `conformance-python-native`, `security-python`, Python metrics, Python
  license/SBOM, and Python resource inventory pass.
- M12: `conformance-ruby-native`, `security-ruby`, Ruby metrics, Ruby
  license/SBOM, and Ruby resource inventory pass.
- M13: README, tutorials, examples, and measured metrics are updated and tested.
- M14: hosted no-env SwiftPM consumer and packaged `.app` smoke evaluate all
  five languages from the release artifact.

## Reviewers

- Engineering Manager
- Architecture Expert Engineer
- GraalVM and Polyglot Runtime Engineer
- Release, Licensing, and Distribution Engineer
- Swift API and Developer Experience Engineer
- Security and Capability Model Engineer
- TDD, Testability, and Verification Engineer
- Unix and Reusable Components Engineer
- Clean Code and Functional Core Engineer
- Technical Debt Steward
- Claude CLI review
