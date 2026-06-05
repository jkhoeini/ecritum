# ADR-008: GraalPy Inclusion Gate and Artifact Policy

Status: Accepted

## Context

Ecritum's plan includes Python support through GraalPy, with no separate Python
installation for SwiftPM consumers. The same plan makes native wheels a v0
non-goal and allows pure-Python packages only from host-approved plugin
directories.

The current local artifact already exceeds ADR-018's initial Core size gates
after SCI, GraalJS, and LuaJ. M5-001 measured:

- artifact directory: 152,317,307 bytes
- private Native Image runtime: 152,175,352 bytes
- public wrapper: 130,096 bytes

GraalPy has an official Maven embedding path, but it is much heavier than the
current language additions. Current official examples use
`org.graalvm.polyglot:python` for the language runtime and
`org.graalvm.python:python-embedding` plus `graalpy-maven-plugin` for packaged
Python resources. GraalVM Native Image documentation says embedded polyglot
language resources are included by default when supported, can be copied next to
the native image with `-H:+CopyLanguageResources`, and can be looked up from a
runtime resource path.

Local dependency evidence for the repo-pinned GraalVM line, 25.0.2:

- `org.graalvm.polyglot:python:25.0.2` depends on
  `org.graalvm.python:python:25.0.2`.
- `org.graalvm.python:python:25.0.2` pulls `python-language`,
  `python-resources`, and `truffle-runtime`.
- `python-language` also pulls Truffle, profiler, regex, NFI, libffi/Panama
  NFI, ICU4J, XZ, and BouncyCastle dependencies.
- After `mise exec -- mvn -s .mvn/settings.xml -q dependency:get
  -Dartifact=org.graalvm.polyglot:python:25.0.2:pom -Dtransitive=true`, local
  cache size was about 127 MB under `org/graalvm/python`, 19 MB under
  `org/graalvm/truffle`, 18 MB under `org/graalvm/shadowed`, and 9.8 MB under
  `org/bouncycastle`.
- Largest downloaded JARs were `python-language-25.0.2.jar` at 102,297,738
  bytes and `python-resources-25.0.2.jar` at 14,976,693 bytes.
- License inventory is not a simple single-license mapping. GraalPy POMs declare
  MIT, Python Software Foundation License, and UPL; transitive dependencies add
  Bouncy Castle licensing and GraalVM profiler tooling under GPL-2.0 with the
  Classpath Exception. Ecritum's license tooling must parse and report every
  declared license expression for all newly shipped Python artifacts before any
  Python artifact is releasable.

Security ADRs already require every Python inclusion to deny direct Java
imports, raw Polyglot bindings, `ctypes`, `cffi`, native wheels and extension
modules, subprocess, raw sockets, direct host filesystem access, environment
access, and native POSIX/platform access by default.

GraalPy's embedding-permissions documentation says the Java POSIX backend is the
default for Java `Context` embeddings and supports Truffle/Polyglot abstraction
providers, while the native POSIX backend bypasses that layer. GraalPy's native
extension documentation also says native extensions run as native binaries and
can circumvent Truffle/JVM protections; embedding them requires native access,
IO, and thread creation permissions that are incompatible with Ecritum's v0
untrusted-scripting boundary.

## Decision

GraalPy is not included in Ecritum Core and Ecritum must not claim Python
support for v0 from M6-001.

GraalPy remains a Full-only candidate until a later implementation spike proves
all of the following in the actual XCFramework shape:

- Native Image builds a shared library with the Python runtime reachable.
- Python standard-library resources are either embedded into the image or copied
  into a deterministic artifact resource directory whose lookup works from a
  clean SwiftPM consumer.
- Size, startup, idle RSS, first-eval, license inventory, and dependency delta
  are measured against the non-Python artifact in the same commit.
- Python conformance and abuse tests pass through the public eval/job/value ABI.
- Release packaging has a Core/Full artifact lane or another accepted policy
  that prevents Python from silently inflating the default artifact.
- Release reproducibility is proved by identical repeated package checksums,
  SwiftPM checksum generation, and clean-consumer/package smoke tests.
- Artifact inspection verifies Python runtime/resource contents instead of
  relying on a hand-maintained embedded-runtime list.
- macOS slice policy is explicit. GraalVM 25.0.2 removes macOS x64 support, so
  any future universal or x86_64 macOS artifact requires a separate toolchain
  decision before it can be promised.

Future GraalPy work must use `org.graalvm.polyglot:python` pinned to the
project's `graalvm.version`. Do not use the deprecated `python-community`
coordinate. Use `org.graalvm.python:python-embedding` and the GraalPy Maven
plugin only after the resource/VFS policy is designed and tested.

Any future context builder must start from deny-by-default permissions:
`allowAllAccess(false)`, `HostAccess.NONE`, no host class lookup, no host class
loading, `PolyglotAccess.NONE`, no native access, no process access, no
environment access, and either `IOAccess.NONE` or an Ecritum-owned virtual
filesystem that enforces the same policy object as the C/Swift host API.

Python standard-library packaging is rejected for Core in M6-001. The official
resource mechanisms are plausible for Full, but M6-001 does not prove that the
stdlib is acceptable in the default artifact.

Pure-Python package loading is allowed only in a future Full spike and only from
host-approved, immutable package roots. Ecritum v0 must not run `pip`, download
packages, mutate `site-packages`, load packages from arbitrary `sys.path`
locations, or write bytecode caches as part of package loading.

Native wheels, C extensions, `ctypes`, `cffi`, NFI/FFI, direct Java imports,
raw Polyglot imports/exports/eval, subprocess, raw sockets, host environment
access, and direct filesystem access outside Ecritum facades are non-goals for
v0 and must be denied by tests before any Python support claim.

The future spike should evaluate GraalPy size-reduction and denial options such
as omitting SSL, digest, compression libraries, native POSIX, Java inet,
platform access, and automatic async actions, but only after each omitted
capability is matched to Ecritum's product surface and abuse tests.

## Consequences

M6-001 is an inclusion gate, not a support implementation. It can complete with
no adapter code because it prevents a false product promise and creates the
acceptance criteria for the later Full-artifact spike.

Swift, C, README, and release docs must continue to describe Python as planned
or gated, not supported.

`just size` remains expected to fail for the current combined local artifact
until the project resolves the Core/Full artifact split or revises ADR-018.
GraalPy cannot be used as a reason to rebaseline Core without packaged-app and
clean-consumer evidence.

ECRITUM-DEBT-0005 remains open until M6-002 also gates Ruby and the project has
both Python and Ruby artifact-policy decisions.

## Alternatives Considered

- Add GraalPy directly to Core. Rejected because the current artifact already
  exceeds Core size gates and the raw GraalPy dependency payload is larger than
  the current language-runtime budget.
- Add a trusted Python eval smoke now and defer sandboxing. Rejected because a
  reachable Python runtime without the ADR-004/ADR-013 denials would create a
  misleading support signal.
- Use GraalPy standalone as a separate executable. Rejected for v0 because
  Ecritum's public integration surface is a SwiftPM binary artifact plus stable
  C ABI, not a sidecar process.
- Use CPython or another native Python embed. Rejected for this task because it
  would change the GraalVM Native Image architecture and requires a separate ABI,
  packaging, sandbox, and licensing design.
- Allow arbitrary pure-Python package directories immediately. Rejected because
  package roots, imports, bytecode caches, resource lookup, and reproducibility
  need a host-approved VFS policy first.

## Verification Plan

For this gate ADR:

- `mise exec -- just plan-check`
- `rg -n "Python.*supported|supports Python|python\\\"\\)" README.md
  PROJECT.org docs Sources Tests native scripts`
- `mise exec -- mvn -s .mvn/settings.xml -q dependency:get
  -Dartifact=org.graalvm.polyglot:python:25.0.2:pom -Dtransitive=true`
- `du -sh ~/.m2/repository/org/graalvm/python
  ~/.m2/repository/org/graalvm/truffle
  ~/.m2/repository/org/graalvm/shadowed
  ~/.m2/repository/org/bouncycastle`

For any future Full-artifact GraalPy spike:

- `mise exec -- mvn -s .mvn/settings.xml -f native/pom.xml dependency:tree
  -Dscope=runtime`
- `mise exec -- just test-java`
- `mise exec -- just native`
- `mise exec -- just test-native-eval-smoke`
- `mise exec -- just xcframework`
- `mise exec -- just test-swift`
- `mise exec -- just conformance-python-native`
- `mise exec -- just security-python`
- `mise exec -- just check-security-static`
- `mise exec -- just check-abi`
- `mise exec -- just check-xcframework`
- `mise exec -- just inspect`
- `mise exec -- just size`
- `mise exec -- just bench-cold-start`
- `mise exec -- just bench-idle-rss`
- `mise exec -- just bench-python-first-eval`
- `mise exec -- just license-report`
- `mise exec -- just license-report-strict`
- `mise exec -- just check-dep-delta`
- `mise exec -- just package-artifact` twice with identical SHA256 output
- `mise exec -- just checksum`
- clean-consumer packaged app smoke for the Full artifact

Minimum Python abuse probes:

- `import java`, `java.type`, `from java import ...`
- `import polyglot`, `polyglot.import_value`, `polyglot.export_value`,
  `polyglot.eval`, and raw Polyglot import/export/eval
- `import ctypes`, `import cffi`, `_ctypes`, NFI/FFI probes
- native extension import probes and wheel/package metadata probes
- `os.system`, `subprocess`, `posix`, `signal`, `platform`
- `socket`, `ssl`, `urllib`, loopback and external network probes
- `open`, `pathlib`, `tempfile`, direct writes, bytecode-cache writes
- `os.environ`, `getpass`, process/user identity probes
- `sys.path` mutation and imports from unapproved directories

## References

- GraalPy JVM developers guide: https://www.graalvm.org/python/jvm-developers/
- GraalVM embedding languages guide:
  https://www.graalvm.org/dev/reference-manual/embed-languages/
- GraalPy Native Image guidance:
  https://www.graalvm.org/jdk23/reference-manual/python/native-applications/
- Maven Central POMs for `org.graalvm.polyglot:python:25.0.2`,
  `org.graalvm.python:python:25.0.2`, and
  `org.graalvm.python:python-embedding:25.0.2`.

## Reviewers

- Engineering Manager
- GraalVM and Polyglot Runtime Engineer
- Release, Licensing, and Distribution Engineer
- Security and Sandboxing Engineer
- TDD, Testability, and Verification Engineer
- Architecture Expert Engineer
- Claude plan and diff review
