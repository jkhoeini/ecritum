# ADR-009: TruffleRuby Inclusion Gate and Artifact Policy

Status: Accepted; next-release artifact policy superseded by ADR-025.

ADR-025 supersedes the Core/Full and Full-only-candidate parts of this ADR for
M9 and later. This ADR remains the historical M6-002 TruffleRuby inclusion gate
and continues to own the Ruby denial requirements until M12 replaces them with
passing implementation evidence.

## Context

Ecritum's plan includes Ruby support through TruffleRuby if Native Image
packaging works. The plan keeps RubyGems and native C-extension support out of
v0 and requires current Maven coordinates to be tracked separately because
TruffleRuby packaging changes over time.

The current local artifact already exceeds ADR-018's initial Core size gates
after SCI, GraalJS, and LuaJ:

- artifact directory: 152,317,307 bytes
- private Native Image runtime: 152,175,352 bytes
- public wrapper: 130,096 bytes

The repo is pinned to GraalVM 25.0.2. Current Maven Central checks found no
`org.graalvm.polyglot:ruby:25.0.2` or
`org.graalvm.ruby:ruby-language:25.0.2` artifacts. The available Maven line was
25.0.0:

- `org.graalvm.polyglot:ruby:25.0.0` depends on
  `org.graalvm.ruby:ruby:25.0.0`.
- `org.graalvm.ruby:ruby:25.0.0` pulls `ruby-language`,
  `ruby-resources`, `truffle-runtime`, and `org.graalvm.llvm:llvm-native`.
- `ruby-language` pulls Ruby annotations/shared artifacts, Truffle API/NFI,
  regex, LLVM API/language NFI/native artifacts, JLine, JCodings, Joni,
  collections, nativeimage, polyglot, and Truffle NFI libffi/Panama artifacts.
- After `mise exec -- mvn -s .mvn/settings.xml -q dependency:get
  -Dartifact=org.graalvm.polyglot:ruby:25.0.0:pom -Dtransitive=true`, local
  cache size was about 53 MB under `org/graalvm/ruby`, 76 MB under
  `org/graalvm/llvm`, 37 MB under `org/graalvm/truffle`, and 41 MB under
  `org/graalvm/shadowed`.
- Largest downloaded JARs were `ruby-language-25.0.0.jar` at 29,063,809 bytes,
  `ruby-resources-25.0.0.jar` at 25,288,061 bytes,
  `llvm-language-native-resources-25.0.0.jar` at 56,512,783 bytes, and
  `llvm-language-25.0.0.jar` at 21,549,564 bytes.
- `ruby-resources-25.0.0.jar` contained 2,643 entries in the local probe,
  including bundled gems, Bundler executables, debug gem C-extension sources,
  network gems such as `net-ftp`/`net-imap`, MRI RubyGems libraries, C-extension
  headers, and nested license files. A Ruby artifact therefore needs resource
  pruning, resource inventory, bundled-gem license inventory, and activation
  tests before it can be releasable.

TruffleRuby's official embedding guidance uses the GraalVM Polyglot API. It says
embedded TruffleRuby is configured to cooperate with another application,
including single-threaded mode by default. The same guidance says embedders may
want `allowNativeAccess(false)` or the experimental `platform-native=false`
option to disable internal NFI use, and `cexts=false` can disable C extensions.

Security ADRs already require every Ruby inclusion to deny Java/Polyglot
modules, NFI, C extensions, native gems, subprocess, raw sockets, direct host
filesystem access, inner contexts, and any host capability outside Ecritum
facades by default.

GraalVM 25.0.2 also removes macOS x64 support. Any future Ruby artifact for
Intel macOS would require a separate toolchain and support-policy decision.

## Decision

TruffleRuby is not included in Ecritum Core and Ecritum must not claim Ruby
support for v0 from M6-002.

TruffleRuby remains a Full-only candidate until a later implementation spike
proves all of the following in the actual XCFramework shape:

- Maven coordinates match the repo's GraalVM version, or an ADR explicitly
  accepts a tested cross-patch version-skew policy.
- Native Image builds a shared library with the Ruby runtime reachable.
- Ruby runtime resources and any standard-library subset are embedded into the
  image or copied into a deterministic artifact resource directory whose lookup
  works from a clean SwiftPM consumer.
- Bundled resources are inventoried and pruned. Any retained bundled gems,
  C-extension headers, platform libraries, network-facing libraries, and helper
  executables are either justified by the Ruby support surface or removed.
- `platform-native=false`, `cexts=false`, no native access, no host class
  lookup, no raw Polyglot access, and no direct IO/network/process/env access
  are proven in Native Image.
- Size, startup, idle RSS, first-eval, license inventory, and dependency delta
  are measured against the non-Ruby artifact in the same commit.
- Ruby conformance and abuse tests pass through the public eval/job/value ABI.
- Release packaging has a Core/Full artifact lane or another accepted policy
  that prevents Ruby from silently inflating the default artifact.
- Release reproducibility is proved by identical repeated package checksums,
  SwiftPM checksum generation, and clean-consumer/package smoke tests.
- Artifact inspection verifies Ruby runtime/resource contents instead of relying
  on a hand-maintained embedded-runtime list.

Ruby standard-library packaging is rejected for Core in M6-002. The official
resource artifacts are plausible for Full, but M6-002 does not prove that the
stdlib or resource layout is acceptable in the default artifact.

Any future context builder must start from deny-by-default permissions:
`allowAllAccess(false)`, `HostAccess.NONE`, no host class lookup, no host class
loading, `PolyglotAccess.NONE`, no native access, no process access, no
environment access, and either `IOAccess.NONE` or an Ecritum-owned virtual
filesystem that enforces the same policy object as the C/Swift host API.

RubyGems, Bundler installation, runtime gem installation, native gems, C
extensions, NFI/FFI, direct Java imports, raw Polyglot imports/exports/eval,
subprocess, raw sockets, environment access, and direct filesystem access
outside Ecritum facades are non-goals for v0 and must be denied by tests before
any Ruby support claim.

Pure-Ruby package loading is allowed only in a future Full spike and only from
host-approved, immutable package roots. Ecritum v0 must not run `gem`, download
gems, mutate `GEM_HOME`, mutate load paths from arbitrary locations, or write
runtime caches as part of package loading.

## Consequences

M6-002 is an inclusion gate, not a support implementation. It can complete with
no adapter code because it prevents a false product promise and creates the
acceptance criteria for the later Full-artifact spike.

Swift, C, README, and release docs must continue to describe Ruby as planned or
gated, not supported.

`just size` remains expected to fail for the current combined local artifact
until the project resolves the Core/Full artifact split or revises ADR-018.
TruffleRuby cannot be used as a reason to rebaseline Core without packaged-app
and clean-consumer evidence.

ECRITUM-DEBT-0005 can be resolved once M6-002 records the Ruby gate decision and
the project has both Python and Ruby artifact-policy decisions.

## Alternatives Considered

- Add TruffleRuby directly to Core. Rejected because the current artifact
  already exceeds Core size gates, matching 25.0.2 Ruby Maven artifacts were not
  available, and the raw available Ruby plus LLVM dependency payload is large.
- Add a trusted Ruby eval smoke now and defer sandboxing. Rejected because a
  reachable Ruby runtime without the ADR-004/ADR-013 denials would create a
  misleading support signal.
- Use the TruffleRuby standalone distribution. Rejected for v0 because Ecritum's
  public integration surface is a SwiftPM binary artifact plus stable C ABI, not
  a sidecar runtime install.
- Use CRuby, mruby, or another Ruby implementation. Rejected for this task
  because it would change the GraalVM Native Image architecture and requires a
  separate ABI, packaging, sandbox, and licensing design.
- Allow arbitrary RubyGems directories immediately. Rejected because gem roots,
  load paths, native gems, cache writes, reproducibility, and clean-consumer
  behavior need a host-approved VFS/resource policy first.

## Verification Plan

For this gate ADR:

- `mise exec -- just plan-check`
- `rg -n "Ruby.*supported|supports Ruby|Ruby support|ruby\"\\)|ruby'\\)|language.*ruby" README.md
  PROJECT.org docs Sources Tests native scripts justfile DEBT.md`
- `curl -I -fsSL
  https://repo1.maven.org/maven2/org/graalvm/polyglot/ruby/25.0.2/ruby-25.0.2.pom`
  expected to return 404 while the repo remains pinned to GraalVM 25.0.2.
- `mise exec -- mvn -s .mvn/settings.xml -q dependency:get
  -Dartifact=org.graalvm.polyglot:ruby:25.0.0:pom -Dtransitive=true`
- `du -sh ~/.m2/repository/org/graalvm/ruby
  ~/.m2/repository/org/graalvm/llvm
  ~/.m2/repository/org/graalvm/truffle
  ~/.m2/repository/org/graalvm/shadowed`

For any future Full-artifact TruffleRuby spike:

- `mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml dependency:tree
  -Dscope=runtime`
- `mise exec -- just test-java`
- `mise exec -- just native`
- `mise exec -- just test-native-eval-smoke`
- `mise exec -- just xcframework`
- `mise exec -- just test-swift`
- `mise exec -- just conformance-ruby-native`
- `mise exec -- just security-ruby`
- `mise exec -- just check-security-static`
- `mise exec -- just check-abi`
- `mise exec -- just check-xcframework`
- `mise exec -- just inspect`
- `mise exec -- just size`
- `mise exec -- just bench-cold-start`
- `mise exec -- just bench-idle-rss`
- `mise exec -- just bench-ruby-first-eval`
- `mise exec -- just license-report`
- `mise exec -- just license-report-strict`
- `mise exec -- just check-dep-delta`
- `mise exec -- just package-artifact` twice with identical SHA256 output
- `mise exec -- just checksum`
- clean-consumer packaged app smoke for the Full artifact

Minimum Ruby abuse probes:

- `Java.type`, `Java.import`, direct Java package/class lookup
- raw Polyglot import/export/eval and inner-context probes
- NFI/FFI, `Fiddle`, C extension, native gem, and native library loading probes
- `Kernel.system`, backticks, `exec`, `spawn`, `IO.popen`, `Open3`
- `TCPSocket`, `UDPSocket`, `Socket`, `Net::HTTP`, loopback and external
  network probes
- `File`, `Dir`, `IO`, `Pathname`, `Tempfile`, direct writes, and symlink probes
- `ENV`, process/user identity probes, `Thread`, `Ractor`, signal handlers, and
  thread creation
- `$LOAD_PATH` mutation, `require`/`load` from unapproved directories,
  RubyGems/Bundler activation, and cache/write probes
- timeout, cancellation, recursion, large allocation, and post-timeout context
  reuse or poisoning probes

## References

- TruffleRuby polyglot and embedding guide:
  https://www.graalvm.org/jdk24/reference-manual/ruby/Polyglot/
- TruffleRuby installation guide:
  https://www.graalvm.org/jdk24/reference-manual/ruby/InstallingTruffleRuby/
- GraalVM embedding languages guide:
  https://www.graalvm.org/dev/reference-manual/embed-languages/
- Maven Central POMs for `org.graalvm.polyglot:ruby:25.0.0`,
  `org.graalvm.ruby:ruby:25.0.0`,
  `org.graalvm.ruby:ruby-language:25.0.0`, and
  `org.graalvm.ruby:ruby-resources:25.0.0`.
- GraalVM 25 release notes:
  https://www.graalvm.org/release-notes/JDK_25/

## Reviewers

- Engineering Manager
- GraalVM and Polyglot Runtime Engineer
- Release, Licensing, and Distribution Engineer
- Security and Sandboxing Engineer
- TDD, Testability, and Verification Engineer
- Architecture Expert Engineer
- Claude plan and diff review
