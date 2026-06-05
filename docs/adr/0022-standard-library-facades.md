# ADR-022: Standard-Library Facades And Policy Bridge

Status: Accepted

Reviewers: Security, GraalVM Runtime, Clean Code, Tests/TDD, Unix, Claude CLI.

## Context

M3-002 proved SCI Clojure eval and explicit host-function projection through
the language-neutral C ABI. M3-003 adds the first Ecritum standard-library
facades named by ADR-004 and ADR-005:

- `ecritum.json`
- `ecritum.time`
- `ecritum.fs`
- `ecritum.http`

ADR-004 requires every dangerous guest capability to flow through:

```text
guest script -> language adapter -> Ecritum facade -> pure policy decision -> named adapter side effect
```

The current implementation parses runtime/context policy in C and stores the
effective policy on the context. `SciClojureEvaluator` only receives source,
source name, host projections, and a private host callback bridge. It does not
receive an effective policy snapshot and it has no `ecritum.*` namespaces.

M3-003 must not expose raw Java, raw JDK classes, raw C handles, raw Swift
objects, raw filesystem/network APIs, or caller-provided SCI options.

## Decision

Add Ecritum standard-library facades through SCI `:namespaces`. The public C
ABI remains unchanged for M3-003; all new host-side behavior uses private
Native Image entrypoints and private C bridge functions.

The M3-003 namespace surface is:

- `ecritum.json/read-string`
- `ecritum.json/write-string`
- `ecritum.time/parse-instant`
- `ecritum.time/format-instant`
- `ecritum.time/now`
- `ecritum.fs/read-text`
- `ecritum.fs/read-bytes`
- `ecritum.fs/exists?`
- `ecritum.http/request`

Supported namespaces must be require-able through a narrow literal grammar.
M3-003 permits only these `require` clauses:

- `(require 'ecritum.json)`
- `(require 'ecritum.time)`
- `(require 'ecritum.fs)`
- `(require 'ecritum.http)`
- a literal vector with `:as`, such as `(require '[ecritum.json :as json])`
- multiple literal clauses in one `require` form when every clause names one
  documented `ecritum.*` namespace

No `:refer`, `:refer :all`, dynamic symbols, computed namespace names, aliases
to non-Ecritum namespaces, nested `require`, `requiring-resolve`, `resolve`,
`ns-resolve`, `load-string`, `load-file`, arbitrary project loading, raw
Clojure/JVM libraries, or mixed allowed/denied require lists are accepted.

## Policy Bridge

C remains the authority for effective runtime/context policy. The C eval worker
creates a copied private standard-library policy manifest for the active
context and passes it into the private Native Image eval entrypoint.

The private manifest is deterministic JSON with this schema:

```json
{
  "schemaVersion": 1,
  "filesystem": {
    "mode": "denied",
    "roots": []
  },
  "network": {
    "mode": "denied",
    "rules": []
  },
  "clock": {
    "mode": "denied"
  },
  "resourceLimits": {}
}
```

Keys are emitted in the order shown, arrays are sorted, duplicate entries are
rejected, UTF-8 and size limits match the public config parser, and unknown
keys fail closed. The manifest may contain only:

- filesystem mode and canonical root strings
- network scheme/host/port rules
- clock toggle
- resource-limit values needed by facade decisions

The manifest must not contain raw C handles, host function pointers, Swift
closures, raw Java objects, raw environment values, raw filesystem contents, or
caller-provided backend options.

Java may parse the private manifest into immutable value objects used for pure
policy decisions. Java must not reparse public policy JSON and must not widen
or reinterpret policy beyond the C manifest.

The side-effect bridge is also private and versioned. Java calls a
`StandardLibraryBridge` operation by name, such as `fs.read_text`,
`fs.read_bytes`, `fs.exists`, `time.now`, or `http.request`. Arguments and
results use the backend wire value codec. C receives only the opaque runtime or
context token already available to the eval worker, the operation name, encoded
arguments, source name, and an output buffer. C returns an encoded
`SciEvalResult`. Permission denial, invalid arguments, bridge buffer overflow,
and internal failures must be represented as structured backend results, not
raw Java exceptions. Buffer limits must be explicit and tested.

## Facade Semantics

`ecritum.json` is pure. It must not use Clojure/EDN readers, `eval`, host
objects, or a shipped dependency such as Jackson or Gson for M3-003. It encodes
and decodes JSON-compatible Ecritum values only: nil, booleans, signed 64-bit
integers, finite doubles, strings, arrays, and string-keyed maps. Non-string
map keys, duplicate JSON object keys, integer overflow, unsupported `data`
values, NaN, and Infinity fail with structured script errors. Output is compact,
has no trailing newline, uses stable numeric spelling, and sorts map keys
lexicographically.

`ecritum.time` is split:

- `parse-instant` and `format-instant` are pure deterministic operations over
  ISO-8601 instants.
- `now` is a clock side effect and requires the clock capability.
- Tests use a deterministic clock adapter.
- Production clock access goes through `StandardLibraryBridge` and a named C
  adapter; facade code must not call `Instant.now()` directly.

`ecritum.fs` is policy-checked:

- default filesystem policy denies every operation
- `read-text`, `read-bytes`, and `exists?` are read operations
- read operations require the effective filesystem mode to be `read_only` or
  `read_write`
- requested paths must be normalized and contained inside one configured root
- `..` traversal, empty paths, NUL bytes, symlink escapes, and absolute paths
  outside the configured roots are denied
- raw `clojure.java.io`, raw `babashka.fs`, `java.io`, `java.nio`, and file
  objects remain unavailable to scripts

For M3-003, C owns filesystem side effects and containment decisions. C
canonicalizes roots at operation time; missing roots do not match. C resolves
the requested path and all symlink components before opening. When platform
APIs allow it, reads should use descriptor-relative opens under the accepted
root. If the implementation cannot prove symlink-swap/TOCTOU safety for an
allowed path, that operation must fail closed and M3-003 cannot claim
filesystem allowed-root completion until descriptor-relative proof or
race-oriented tests exist. Java facade functions call the private
`StandardLibraryBridge` so C can enforce effective policy immediately before
the operation.

`ecritum.http/request` is present but default-deny-only in M3-003:

- default network policy denies every request
- configured `network.allowed` does not enable real outbound HTTP in M3-003
- any production `http/request` side effect returns a structured permission
  denial until a later ADR accepts real HTTP behavior
- raw sockets, loopback/link-local/multicast broadening, wildcard hosts, DNS
  broadening, redirects, TLS settings, and
  raw Java HTTP clients are not exposed
- deterministic fake-adapter tests may exercise request shape and redirect
  denial, but they are not release evidence for real network side effects

Real HTTP side effects require a later ADR or ADR addendum with exact
scheme/host/port matching, redirect recheck, DNS and loopback/link-local
policy, TLS behavior, secret redaction, Native Image cost, dependency impact,
and native artifact tests.

## Error Semantics

Policy denial from any facade maps to `ECRITUM_ERROR_PERMISSION_DENIED` with:

- operation `eval`
- language `clojure`
- source name preserved
- category `permission`
- redacted diagnostic message

Facade misuse such as invalid argument shape maps to `ECRITUM_ERROR_SCRIPT`
with category `runtime` unless a more specific existing status is already
defined. Internal bridge failures map to `ECRITUM_ERROR_INTERNAL` and must not
leak raw paths outside host-provided source names, tokens, URLs with secrets,
Java class names, stack traces, raw handles, or environment values.

Java facade code uses a dedicated `StandardLibraryException` or equivalent
result carrier for permission denials, invalid arguments, and bridge failures.
Those errors must not fall through as generic SCI/JVM runtime exceptions.

## Implementation Shape

Java owns:

- `SciNamespaceInstaller` for `ecritum.*`
- `StandardLibraryFacade` vars
- `StandardLibraryValueCodec`
- immutable `StandardLibraryPolicy` manifest parsing
- bridge interfaces for side-effect adapters
- pure JSON encode/decode
- pure time parse/format

`SciClojureEvaluator` may wire these pieces together, but it must not become
the implementation home for JSON parsing, policy decisions, or side-effect
adapters.

C owns:

- effective policy snapshot creation for each eval job
- filesystem containment and filesystem side effects
- production clock and HTTP side-effect adapters when enabled
- conversion between backend wire values and public Ecritum values
- mapping bridge failures into structured backend results

Tests may use fake Java adapters for pure unit tests, but production side
effects must go through the C bridge.

## Verification

M3-003 is complete only when these pass:

- Java unit tests for namespace installation and facade semantics
- C/native smoke tests for default-deny, filesystem inside-root success,
  outside-root denial, traversal denial, symlink denial, and race-oriented
  containment behavior or explicit fail-closed evidence
- Swift XCFramework smoke tests for facade eval and structured permission
  errors
- exact-require tests proving supported `ecritum.*` requires work while
  dynamic/arbitrary require remains denied
- strict Clojure conformance with explicit case IDs and expected actuals for:
  `stdlib.json.roundtrip`, `stdlib.time.parse_format`,
  `stdlib.time.now_default_denied`, `stdlib.fs.default_denied`,
  `stdlib.fs.allowed_root_inside`, `stdlib.fs.allowed_root_outside_denied`,
  and `stdlib.http.default_denied`
- Clojure-native security abuse provider with strict `pending=0`,
  `securityConformant=true`, raw IO/network/JDK escape-hatch probes, require
  bypass probes, and facade traversal/symlink/race probes
- `mise exec -- just test-facades-clojure`: writes deterministic JSON to
  `build/facades/clojure.json`, sends diagnostics to stderr, and exits nonzero
  on failure
- `mise exec -- just conformance-clojure-facades`: reuses
  `scripts/run-conformance.py`, writes
  `build/conformance/clojure-facades.json`, and requires strict zero-pending
  facade cases
- `mise exec -- just security-clojure-facades`: reuses
  `scripts/run-security-abuse.py`, writes
  `build/security/clojure-facades.json`, and requires strict
  `securityConformant=true`
- `mise exec -- just test-m3-003`: composes `native`, `test-java`,
  `test-c-abi-eval`, `test-c-abi-eval-asan`, `test-c-abi-policy-config`,
  `test-c-abi-policy-config-asan`, `test-native-eval-smoke`,
  `test-native-eval-smoke-asan`, `xcframework`,
  `test-xcframework-eval-smoke`, `test-facades-clojure`,
  `conformance-clojure-facades`, `security-clojure-facades`, `security`,
  `check-abi`, `license-report`, and `check-dep-delta`
- Native Image static checks showing no new wildcard resources, broad
  reflection/JNI/proxy config, `allowAllAccess`, unrestricted host lookup, or
  new shipped dependencies without release review
- size delta recorded; existing size blockers remain tracked separately

## Consequences

This keeps Ecritum's public integration surface stable while introducing a
script-visible standard library. It deliberately avoids the easy but unsafe
path of giving SCI raw Java/JDK access or reparsing public policy in Java.

The tradeoff is more bridge code and stricter tests before useful filesystem
behavior is available. HTTP remains default-deny-only in M3-003. That cost is
accepted because the standard library is the capability boundary for future
language runtimes, not just a Clojure convenience layer.
