# ADR-020: Versioned Policy Config Schema

Status: Accepted

Reviewers: Security, Architecture, Clean Code, Tests/TDD, Swift DX, Claude CLI.

## Context

ADR-002 requires `config_json` and future options inputs to be Ecritum schemas,
not raw GraalVM or Polyglot option passthroughs. ADR-016 defines the public Swift
policy shape and requires deterministic serialization to the same deny-by-
default C configuration schema. ADR-014 defines resource limits and narrowing.

M2-003 deliberately made non-empty runtime/context config fail closed until a
versioned policy parser landed. M2-005 replaces that stub with pure config data,
serialization, parsing, and narrowing validation. M2-005 does not implement
filesystem, network, process, environment, or language-runtime enforcement.
ADR-004 remains the later M2.5 enforcement and untrusted-script threat-model ADR.

## Decision

Ecritum config schema v1 is a versioned JSON object passed to:

```c
int ecritum_runtime_create(ecritum_bytes_t config_json, ecritum_runtime_t *out_runtime, ecritum_error_t *out_error);
int ecritum_context_create(ecritum_runtime_t runtime, ecritum_bytes_t config_json, ecritum_context_t *out_context, ecritum_error_t *out_error);
```

No new public C symbols or status codes are required for M2-005. The statuses
used by this ADR already exist in the public header:
`ECRITUM_ERROR_INVALID_ARGUMENT`, `ECRITUM_ERROR_INVALID_UTF8`,
`ECRITUM_ERROR_INPUT_TOO_LARGE`, `ECRITUM_ERROR_INVALID_CONFIG`, and
`ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION`.

Empty `config_json` means `config_json.len == 0` regardless of whether
`config_json.data` is `NULL` or non-`NULL`. Empty config remains valid for both
public create entry points, but the default is entry-point specific.

For `ecritum_runtime_create`, empty config means schema v1 runtime defaults:

```json
{
  "schemaVersion": 1,
  "languages": [],
  "policy": {
    "filesystem": { "mode": "denied" },
    "network": { "mode": "denied" },
    "process": { "mode": "denied" },
    "environment": { "mode": "denied" },
    "clock": { "mode": "denied" },
    "random": { "mode": "denied" },
    "log": { "mode": "denied" }
  },
  "diagnostics": { "mode": "redacted" },
  "resourceLimits": {}
}
```

For `ecritum_context_create`, empty config means no context narrowing. The
effective context config inherits the parent runtime config exactly. Empty
context config does not mean a new default-deny runtime-shaped object because
context config cannot contain runtime-only `languages` or `diagnostics`.

Swift `EcritumRuntime.Configuration.default` uses the runtime defaults and
serializes to canonical JSON when a non-empty runtime config must be handed to C.
Swift `EcritumContext.Configuration.default` serializes as empty bytes unless it
contains explicit narrowing.

## Schema Rules

Config validation order is deterministic:

1. `config_json.data == NULL && config_json.len > 0` returns
   `ECRITUM_ERROR_INVALID_ARGUMENT`.
2. `config_json.len == 0` returns the entry-point default: runtime default-deny
   config for runtime creation, or inherited parent runtime config for context
   creation.
3. `config_json.len > 65536` returns `ECRITUM_ERROR_INPUT_TOO_LARGE`.
4. Invalid UTF-8 returns `ECRITUM_ERROR_INVALID_UTF8`.
5. Invalid JSON returns `ECRITUM_ERROR_INVALID_CONFIG`.
6. Schema validation returns `ECRITUM_ERROR_INVALID_CONFIG` or
   `ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION`.

The 64 KiB cap is measured in raw input bytes before UTF-8 decoding or JSON
parsing.

Config input must be valid UTF-8 JSON. The top-level value must be an object.
Non-empty config is capped at 64 KiB before parsing.

- JSON nesting depth is capped at 16.
- Any array is capped at 256 items.
- Any string is capped at 4096 UTF-8 bytes.
- Language, environment key, host, scheme, mode, kind, and resource-limit field
  strings are capped at 255 UTF-8 bytes.
- `schemaVersion` is required when config is non-empty.
- `schemaVersion` must be written as a JSON number using decimal digits only,
  with no sign, decimal point, or exponent notation, and it must be in
  `1...UInt32.max`.
- `schemaVersion == 1` is the only supported version in M2.
- In-range `schemaVersion > 1` returns
  `ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION`.
- `schemaVersion == 0`, negative values, fractions such as `1.0`, exponent
  notation such as `1e0`, values larger than `UInt32.max`, missing versions,
  invalid JSON, duplicate keys, unknown keys, unknown enum values, and
  conflicting rules return `ECRITUM_ERROR_INVALID_CONFIG`.
- `null` is invalid for every v1 field.
- `{}` is a non-empty config object with a missing version, so it is invalid.
  Empty bytes, not `{}`, are the C ABI spelling for default deny.
- Invalid UTF-8 returns `ECRITUM_ERROR_INVALID_UTF8`.
- Configs larger than 64 KiB return `ECRITUM_ERROR_INPUT_TOO_LARGE`.
- Raw GraalVM, Polyglot, JVM, host-access, classpath, filesystem, process, or
  native-library options are not schema keys and fail closed as unknown keys.

The C wrapper must validate non-empty config before creating a runtime/context.
If validation fails, no runtime/context handle is allocated.

Runtime config v1 permits only these top-level keys:

- `schemaVersion`
- `languages`
- `policy`
- `diagnostics`
- `resourceLimits`

Context config v1 permits only:

- `schemaVersion`
- `policy`
- `resourceLimits`

`languages` is required for runtime config and invalid for context config.
`policy`, `diagnostics`, and `resourceLimits` are required for runtime config.
`policy` and `resourceLimits` are optional for context config; missing context
sections inherit runtime effective config.
`languages` in context config is rejected as an unknown key for that schema
scope.

Runtime `policy` permits only these keys and requires all of them in non-empty
runtime config:

- `filesystem`
- `network`
- `process`
- `environment`
- `clock`
- `random`
- `log`

Context `policy` permits the same keys but treats each key as an optional
narrowing section. A missing context policy key inherits the parent runtime
section. An empty context policy object is valid and means no policy narrowing.
Unknown policy keys fail closed in both runtime and context config.

Runtime `resourceLimits` permits only the resource-limit keys defined below and
may be empty. Context `resourceLimits` permits the same keys as optional
narrowing fields. Missing context resource-limit keys inherit the parent runtime
effective values. An empty context resource-limit object is valid and means no
resource-limit narrowing.

The M2 parser is implemented in the C wrapper as a small internal schema parser
with no new shipped dependency unless a separate dependency/license ADR accepts
one. Parser and validator helpers are pure with respect to the global handle
registry: they take bytes and parent config data as input, return parsed config
or a typed status/error description, and do not allocate handles, mutate the
registry, call GraalVM, read the filesystem, inspect the environment, use clocks
or randomness, or run host callbacks. Public entry points parse and validate
before taking the registry lock or allocating runtime/context handles.

Duplicate-key rejection is part of parser scope. If a third-party parser is
introduced later, M2 still requires an explicit duplicate-key detection pass
unless that parser proves duplicate rejection in tests. The initial M2 parser is
expected to track object keys while parsing or while walking the parsed tree and
reject duplicates at every object level.

## Swift Policy Values

Swift exposes pure value types:

- `EcritumConfigurationSchemaVersion` with `UInt32` raw values
- `EcritumPermissionPolicy`
- `EcritumPermissionPolicy.Narrowing`
- `EcritumResourceLimits`
- `EcritumResourceLimits.Narrowing`
- `EcritumDiagnosticsPolicy`
- `EcritumLanguage`

`EcritumRuntime.Configuration` contains languages, policy, diagnostics, and
resource limits. `EcritumContext.Configuration` contains policy/resource
narrowing only.

Policy values expose immutable copy builders for primary developer ergonomics:

```swift
let policy = EcritumPermissionPolicy.defaultDeny
    .withFilesystem(.readOnly(roots: [.directory(appScriptsURL)]))
    .withNetwork(.allowing([.https(host: "api.example.com", port: 443)]))
```

Stored properties may remain public values where useful for advanced setup and
tests, but README examples use the builder form.

Serialization is a pure transformation:

```text
Configuration -> canonical UTF-8 JSON bytes
```

It performs no filesystem reads, path resolution, environment access, clock or
random reads, C allocation, GraalVM calls, global mutation, or runtime handle
access.

## Policy Data

Every capability defaults to denied.

Runtime `languages` is an array of strings. It may be empty. Language names use
the same ASCII identifier grammar as other public names:
`[A-Za-z][A-Za-z0-9_]*`. Duplicate names are invalid. Runtime language
selection is only data in M2-005; unsupported-language errors are owned by eval
and language-runtime tasks.

Filesystem grants are data only in M2-005:

- denied
- read-only roots
- read-write roots

Filesystem policy JSON uses explicit tags:

```json
{ "mode": "read_only", "roots": [{ "kind": "directory", "path": "/absolute/path" }] }
```

Exact filesystem shapes:

- denied: `{ "mode": "denied" }`
- read only: `{ "mode": "read_only", "roots": [root...] }`
- read write: `{ "mode": "read_write", "roots": [root...] }`
- root: `{ "kind": "directory", "path": "/absolute/path" }`

`roots` is required for `read_only` and `read_write`, must be non-empty, and
cannot contain duplicates. M2 validates path strings by rejecting empty path
components, `.` components, `..` components, repeated slashes, trailing slash
except `/`, and non-absolute paths. M2-005 stores and compares paths as opaque
validated strings. It does not normalize paths, resolve symlinks, read
security-scoped bookmarks, or perform filesystem containment checks. Path
canonicalization, symlink race behavior, and security-scoped macOS location
handling must be specified and enforced by ADR-004/M2.5 before filesystem APIs
can use these grants.

Network grants are data only:

- denied
- allowed rules with scheme, host, and port

Network policy JSON uses explicit tags such as:

```json
{ "mode": "allowed", "rules": [{ "scheme": "https", "host": "api.example.com", "port": 443 }] }
```

Exact network shapes:

- denied: `{ "mode": "denied" }`
- allowed: `{ "mode": "allowed", "rules": [rule...] }`
- rule: `{ "scheme": "https", "host": "api.example.com", "port": 443 }`

`rules` is required for `allowed`, must be non-empty, and cannot contain
duplicates. Schemes are lowercase ASCII identifiers. Hosts are non-empty ASCII
names without wildcards. Ports are integers in `1...65535`.

Wildcard hosts, raw sockets, redirect behavior, loopback special cases, and DNS
resolution are enforcement concerns deferred to ADR-004/M2.5.

Process grants are data only:

- denied
- exact executable rules when process support is later accepted

Exact process shapes:

- denied: `{ "mode": "denied" }`
- allowed: `{ "mode": "allowed", "commands": [command...] }`
- command: `{ "path": "/absolute/executable" }`

`commands` is required for `allowed`, must be non-empty, and cannot contain
duplicates.

M2-005 does not expose shell strings, PATH lookup, process execution, or
argument expansion. Command paths must be absolute strings and must not contain
NUL. Shell metacharacters are not interpreted because the policy stores command
identity, not shell text.

Environment grants are data only:

- denied
- exact key allowlist

Exact environment shapes:

- denied: `{ "mode": "denied" }`
- allowed: `{ "mode": "allowed", "keys": ["NAME"] }`

`keys` is required for `allowed`, must be non-empty, and cannot contain
duplicates.

Keys must be non-empty ASCII identifiers and must not contain `=`, NUL, or
wildcards. Listing all variables is not represented.

Clock, random, and log policy shapes:

- denied: `{ "mode": "denied" }`
- allowed: `{ "mode": "allowed" }`

Diagnostics are host-only config and default to redacted. Exact diagnostics
shapes:

- redacted: `{ "mode": "redacted" }`
- raw: `{ "mode": "raw" }`

`raw` diagnostics are accepted as host configuration data only; user-facing
diagnostic leakage remains governed by ADR-002 and later enforcement work.

## Resource Limits

M2 config can represent resource limits as pure optional numeric values:

- `executionTimeoutNanos`
- `maxInputBytes`
- `maxOutputBytes`
- `maxStackDepth`
- `maxHeapBytes`
- `maxCallbackQueueLength`
- `callbackTimeoutNanos`

Missing resource limits inherit from the parent/default. `0` is a valid finite
limit where ADR-014 defines immediate timeout/no wait behavior. All resource
limit values are unsigned integers encoded as JSON numbers and must fit in
`uint64_t`. `UINT64_MAX` is reserved for duration-like limits and invalid until
a later ADR defines an infinite sentinel. Duplicate resource-limit fields are
invalid.

M2-005 represents and validates limits. It does not claim enforcement.

## Runtime And Context Narrowing

Runtime configuration is the maximum grant. Context configuration can only
narrow runtime configuration:

- denied is narrower than any grant
- equal grants are allowed
- subsets of filesystem roots, network rules, process commands, environment
  keys, and resource limits are allowed
- more permissive modes, extra roots/rules/keys/commands, or larger numeric
  limits fail closed

Context configuration does not include language selection in M2. Runtime
languages are fixed at runtime creation until a later ADR introduces per-context
language narrowing.

Context widening returns `ECRITUM_ERROR_INVALID_CONFIG` in M2-005 because the
configuration is contradictory. Later enforcement APIs may return
`ECRITUM_ERROR_PERMISSION_DENIED` for actual denied script operations.
M2-005 relies on structured error details and operation messages to distinguish
specific invalid-config causes such as malformed JSON, unknown keys, and context
widening. A widening-specific public status is deferred until a caller need
appears because no script operation enforcement exists yet.

The C registry stores validated runtime policy/config data so later context
creation can compare narrowing. Context records store their effective narrowed
config for later eval/runtime enforcement work.

## Canonical JSON

Swift serialization uses a dedicated owned canonical writer rather than relying
on incidental `Codable`, `JSONEncoder`, dictionary, or synthesized enum ordering.
Swift serialization emits deterministic JSON:

- stable key order
- no insignificant whitespace
- arrays sorted where order does not carry meaning
- omitted optional resource limits when unset
- all default-deny policy sections included for runtime configs
- explicit tagged objects instead of enum-case-name JSON

All arrays in v1 are sets, not ordered priority lists:

- `languages`
- filesystem `roots`
- network `rules`
- process `commands`
- environment `keys`

Canonical JSON sorts these sets by their canonical element JSON bytes. Duplicate
set elements are invalid. Future ordered rule semantics require a new schema
version.

Canonical JSON exists for tests and ABI handoff stability. C callers may provide
equivalent non-canonical JSON, but it still must satisfy schema validation and
duplicate-key rejection.

## Consequences

M2-005 can unblock the shared conformance-suite work without waiting for the
M2.5 sandbox enforcement ADR. It creates a stable data contract but makes no
claim that guest code is sandboxed until language runtime enforcement and abuse
tests exist.

The C wrapper needs enough JSON validation to reject invalid UTF-8, invalid JSON,
unknown keys, duplicate keys, unsupported versions, and context widening. It
does not need a new public ABI symbol.

Swift gains public policy/config value types. Existing `EcritumRuntime()` and
`EcritumRuntime(.default)` continue to work through defaults.

## Alternatives Considered

Waiting for ADR-004 before M2-005 was rejected because ADR-004 owns enforcement
and the in-process threat model, while M2-005 owns schema parsing and pure data.

Passing raw GraalVM/Polyglot options through config was rejected by ADR-002 and
ADR-016.

Validating config only in Swift was rejected because C is the stable ABI and must
fail closed for non-Swift callers.

Adding new C symbols for policy creation was rejected for M2-005 because the
existing create functions already carry config bytes.

Eager filesystem canonicalization during Swift serialization was rejected because
it would introduce filesystem side effects into a pure configuration transform.

## Verification Plan

Implementation must add red-first tests for:

- default-deny Swift serialization
- filesystem, network, process, environment, diagnostics, and resource-limit
  data serialization
- deterministic canonical JSON
- invalid JSON, invalid UTF-8, duplicate keys, unknown keys, unknown enum values,
  unsupported versions, and missing schema version
- missing required non-empty runtime keys such as `languages`, `policy`,
  `diagnostics`, or `resourceLimits`
- `null` rejection for every v1 field
- C ABI empty config variants: `NULL + len 0` and non-`NULL + len 0`
- runtime empty config producing default-deny policy data
- context empty config inheriting the parent runtime effective config without
  extra narrowing
- `NULL + len > 0`
- config larger than 64 KiB
- nesting depth greater than 16
- arrays greater than 256 items
- strings greater than 4096 UTF-8 bytes
- language, environment key, host, scheme, mode, kind, and resource-limit field
  strings greater than 255 UTF-8 bytes
- schema version edge cases: `0`, `-1`, `1.0`, `1e0`, values larger than
  `UInt32.max`, booleans, and strings
- duplicate set elements in languages, filesystem roots, network rules, process
  commands, and environment keys
- path validation for NUL, `.`, `..`, repeated slashes, non-absolute paths, and
  trailing slash
- C runtime creation with valid non-empty config
- C runtime creation failures allocate no handle
- C context creation accepting narrowed filesystem, network, environment, and
  process subsets and rejecting widening for each
- C context creation accepting equal or smaller resource-limit values and
  rejecting larger resource-limit values
- C context creation accepting clock, random, and log equality/denial narrowing
  and rejecting denied-to-allowed widening for each
- C context creation rejecting runtime-only keys such as `languages` and
  `diagnostics`
- empty config backward compatibility
- artifact-backed Swift runtime/context creation with explicit default config

Required commands:

```text
mise exec -- just test-swift-scaffold
mise exec -- just test-c-abi-lifecycle
mise exec -- just test-c-abi-asan
mise exec -- just check-abi
mise exec -- just xcframework
mise exec -- just check-xcframework
mise exec -- just test-swift
mise exec -- just test
```
