# ADR-016: Swift API And Permission Policy Shape

Status: Accepted

Reviewers: Swift DX, Architecture, Security, Clean Code, Tests/TDD, Claude CLI.

## Context

Ecritum's first-party developer experience is Swift, but the stable integration
surface remains the C ABI from ADR-002. The README and PLAN currently show
conflicting Swift permission sketches: a flat `permissions: [.filesystemRead]`
shortcut, an `EcritumPermissions: OptionSet`, and richer associated-value
policies. M2 needs one canonical Swift shape before runtime/context/value work
starts.

ADR-002 also constrains the Swift API:

- Swift wrappers own private C handles and never expose raw `ecritum_*` handles.
- `close()`/`deinit` must be idempotent and map to pointer-to-handle C destroy
  functions.
- Runtime/context/script wrappers must not claim `Sendable` until explicit
  synchronization or actor isolation plus tests exist.
- Swift errors must be structured, not raw integer status wrappers.
- Swift config must serialize to Ecritum's versioned, deny-by-default C config
  schema. Raw GraalVM/Polyglot options are not public API.

## Decision

The canonical Swift creation input is `EcritumRuntime.Configuration`.

```swift
public final class EcritumRuntime {
    public struct Configuration: Equatable, Sendable {
        public var languages: Set<EcritumLanguage>
        public var policy: EcritumPermissionPolicy
        public var diagnostics: EcritumDiagnosticsPolicy
        public var resourceLimits: EcritumResourceLimits
    }

    public init(_ configuration: Configuration) throws
    public func context(_ configuration: EcritumContext.Configuration) throws -> EcritumContext
    public func namespace(_ name: EcritumNamespace.Name) throws -> EcritumNamespace
    public func close() throws
}

public final class EcritumContext {
    public struct Configuration: Equatable, Sendable {
        public var policy: EcritumPermissionPolicy.Narrowing
        public var resourceLimits: EcritumResourceLimits.Narrowing
    }

    public func eval(_ script: EcritumScript) async throws -> EcritumValue
    public func close() throws
}
```

Runtime and context wrappers are final reference types that privately store C
handle values. `EcritumContext` holds its parent `EcritumRuntime` strongly.
Context policy may only narrow the runtime policy; it cannot widen it.

`close()` is `throws` because ADR-002 destroy calls can fail with live children,
busy operations, or teardown failure. `close()` is idempotent. A second close is
a no-op. Post-close operations throw a typed `.closed` or `.useAfterClose`
error. `deinit` performs best-effort close if still open; failures are reported
only through configured host diagnostics because Swift deinitializers cannot
throw.

`EcritumRuntime`, `EcritumContext`, `EcritumNamespace`, and any future compiled
script handle are not `Sendable` in M2. A later implementation may add
`Sendable` only with explicit synchronization or actor isolation, fake-adapter
concurrency tests, and TSan coverage.

## Language Type

`EcritumLanguage` is an extensible string-backed value type, not a closed enum.

```swift
public struct EcritumLanguage: RawRepresentable, Hashable, Codable, Sendable {
    public let rawValue: String

    public init(rawValue: String)

    public static let clojure: Self
    public static let javascript: Self
    public static let lua: Self
    public static let python: Self
    public static let ruby: Self
}
```

The type is extensible because the C ABI receives language names as string
views, and future wrappers or plugins may need names not known when the Swift
package was compiled. Extensibility is not a support promise: unsupported or
unpackaged languages return typed unavailable-language errors. README and PLAN
must not imply Python or Ruby are in the default artifact until their inclusion
gates pass.

## Script Type

`EcritumScript` is a pure Swift value descriptor in M2, not a compiled C handle.

```swift
public struct EcritumScript: Equatable, Sendable {
    public var source: String
    public var language: EcritumLanguage
    public var sourceName: String?
    public var options: EcritumScriptOptions
}
```

`EcritumContext.eval(_:)` serializes the descriptor to the C ABI using
pointer-plus-length views from ADR-002. The descriptor performs no runtime work,
filesystem reads, C allocation, or GraalVM interaction. A compiled script handle
may be added after ADR-003 defines async/job/executor lifecycle and after the C
ABI has an owned script handle.

## Namespace Registration

The canonical host capability API is namespace-based.

```swift
let app = try runtime.namespace(.init("app"))

try app.register(.init("notify")) { call in
    let message = try call.string(at: 0)
    notifier.enqueue(message)
    return .null
}
```

`EcritumNamespace.Name` and `EcritumFunctionName` are validated value types.
M2 grammar is ASCII:

- namespace segments: `[A-Za-z][A-Za-z0-9_]*`
- namespaces may contain `.` between segments
- function names: `[A-Za-z][A-Za-z0-9_]*`
- names beginning with `ecritum`, `java`, `javax`, `sun`, `graal`, or `truffle`
  are reserved unless a later ADR grants an explicit standard-library namespace

Flat `runtime.register("app.notify", ...)` is not canonical in M2. It may become
sugar later, but the underlying model is validated namespace plus function
descriptors. Language-specific projections, such as Clojure `app/notify` or
JavaScript `ecritum.app.notify`, are deterministic mappings from the same
descriptor and must be tested.

M2 host callbacks are synchronous:

```swift
public typealias EcritumHostFunction =
    @Sendable (EcritumCall) throws -> EcritumValue
```

Async host callbacks depend on ADR-003 executor/job behavior and are not public
M2 API.

## Permission Policy

Public `OptionSet` permissions are rejected. The public policy is an immutable,
versioned, Codable value object that serializes deterministically to the same C
configuration schema used by the native runtime.

```swift
public struct EcritumPermissionPolicy: Codable, Equatable, Sendable {
    public var schemaVersion: UInt = 1
    public var filesystem: FilesystemPolicy = .denied
    public var network: NetworkPolicy = .denied
    public var process: ProcessPolicy = .denied
    public var environment: EnvironmentPolicy = .denied
    public var clock: ClockPolicy = .denied
    public var random: RandomPolicy = .denied
    public var log: LogPolicy = .denied
}
```

`EcritumPermissionPolicy` has a `defaultDeny` value. Every dangerous capability
is denied unless the host explicitly grants a narrow rule. Unknown schema
versions, unknown keys, unknown enum cases, invalid UTF-8, invalid JSON,
conflicting rules, and context attempts to widen runtime policy fail closed
before any runtime is created.

Swift policy serialization is pure:

```swift
Configuration -> Result<SerializedEcritumConfig, EcritumConfigurationError>
```

Serialization performs no runtime calls, filesystem reads, C allocation,
environment lookup, global mutation, or GraalVM interaction. It emits
canonical JSON with stable key order for tests and ABI handoff.

### Filesystem

Filesystem defaults to `.denied`. Grants use explicit canonical roots and modes:
read-only, read-write, temp, or plugin-owned. There is no unrestricted read or
write flag. Symlinks, `..` traversal, and security-scoped macOS locations must
be resolved by the implementation before access. ADR-004 finalizes the threat
model, but the Swift API cannot expose broad filesystem flags.

### Network

Network defaults to `.denied`. Grants use scheme, host, and port rules.
Redirects are rechecked against policy. Loopback, raw sockets, and wildcard
hosts are denied unless explicitly allowed by a narrow rule.

### Process And Environment

Process execution defaults to `.denied`. No shell strings, `PATH` lookup, or
broad process flag exists. If process support is accepted, it uses exact
allowlisted executables, argument policy, environment policy, working-directory
policy, and timeout policy.

Environment defaults to `.denied`. Grants list exact variable names. There is no
wildcard and no API for listing all variables. Errors must not leak variable
values.

### Diagnostics

Diagnostics are host-only configuration, not script-grantable permission.
Default diagnostics are redacted. Raw/debug diagnostics require an explicit
trusted-host opt-in and still must respect ADR-002's default rule: no host paths,
raw Java `Throwable` class names, environment values, process commands, or
guest source text in user-facing errors.

### Dangerous Capabilities

Swift exposes no public policy that enables raw Java lookup, reflection, class
loading, native library loading, arbitrary JAR/classpath mutation, raw GraalVM
host access, Node `fs`/`net`/`child_process`, native Python wheels, Ruby native
extensions, or general FFI. Later ADRs must explicitly accept any such surface.

## Error Mapping

Swift errors are structured values. `EcritumError.runtimeCallFailed(status:)` is
M1 scaffolding and not the M2 API.

M2 Swift errors must map every C status to a typed case that preserves:

- status code
- stable category
- redacted message
- operation
- language and source name when present
- line/column and stack frames only when a future ABI supplies them

Swift copies borrowed error strings/views before destroying the C error handle.
Permission and script errors should have useful user-facing messages without
leaking implementation details.

## README And PLAN

README and PLAN examples must use the canonical policy API or be explicitly
marked as sketches. The old `permissions: [.filesystemRead]` and
`EcritumPermissions: OptionSet` examples are not canonical. Runtime and context
examples must not claim `Sendable`. Async host callbacks must be marked
future/ADR-003-dependent.

## Verification Requirements

ADR-016 is accepted only when README and PLAN no longer present `OptionSet`
permissions as canonical public API.

Implementation tasks must add tests for:

- default-deny policy serialization
- filesystem roots and mode serialization
- network scheme/host/port allowlists
- process command allowlists, argument policy, cwd policy, env policy, and
  timeout policy if process support is implemented
- environment key allowlists
- clock, random, log, diagnostics, and resource limits
- unknown keys, unsupported versions, invalid JSON/UTF-8, duplicate and
  conflicting rules
- deterministic canonical JSON passed to the C ABI
- runtime policy as maximum grant and context narrowing only
- `close()` exactly once, double close no-op, post-close typed errors, `deinit`
  cleanup, live-child close failure, teardown failure handling, and parent
  runtime retention
- `EcritumScript` descriptor behavior, source name preservation, permissions
  snapshot or recheck behavior, and future compiled-script lifecycle if added
- namespace grammar, reserved names, duplicate registration, registration after
  close, failed registration ownership, and language projections
- `EcritumLanguage` raw names, static known values, unsupported language errors,
  unavailable packaged runtime errors, and custom language validation
- no `Sendable` or `@unchecked Sendable` claim without synchronization tests and
  TSan
- every C status to Swift `EcritumError` mapping, owned error destroy exactly
  once, and borrowed strings copied before destroy
- README/SwiftHost examples compile against the public Swift API once
  implementation starts

Required gates for implementation milestones: `mise exec -- just test`,
`mise exec -- just check-abi`, and `mise exec -- just examples`, plus the
sanitizer/leak/TSan gates required by ADR-002 once lifecycle code exists.

## Consequences

Swift API ergonomics remain explicit rather than magical. Hosts write a little
more policy code, but the result is serializable, testable, and aligned with the
native trust boundary. The API avoids making unsupported language or concurrency
promises while preserving extensibility for future runtime artifacts.
