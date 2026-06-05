# ADR-019: Host Function Registration Lifecycle

Status: Accepted

Reviewers: Architecture Expert, Clean Code, Security, Tests/TDD, Swift DX,
Claude CLI.

## Context

ADR-002 fixes callback ownership rules: `user_data` transfers only after
successful registration, failed registration must not destroy it, and
registered `user_data` is destroyed exactly once on unregister or runtime
destroy. ADR-003 fixes the async job and callback reentrancy model. ADR-016
fixes the Swift namespace/function API shape.

M2-004 implements host function registration before eval and job dispatch
exist. The implementation must prove registration lifecycle and callback box
ownership without claiming that guest scripts can call host functions yet.

## Decision

M2-004 is registration lifecycle only. It does not expose eval, jobs, public
callback invocation, argument accessors, or C value conversion.

The public C ABI adds opaque handles:

```c
typedef uint64_t ecritum_namespace_t;
typedef uint64_t ecritum_function_t;
typedef uint64_t ecritum_value_t;
typedef uint64_t ecritum_call_t;
```

`ecritum_value_t` and `ecritum_call_t` are reserved now because the host
callback typedef needs their names, but value/call accessors are implemented by
later value/eval tasks. Raw handles are never exposed through public Swift APIs.

The callback ABI is:

```c
typedef int (*ecritum_host_fn_t)(
    ecritum_call_t call,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error,
    void *user_data
);

typedef void (*ecritum_user_data_destroy_fn_t)(void *user_data);
```

Registration APIs:

```c
int ecritum_namespace_create(
    ecritum_runtime_t runtime,
    ecritum_string_view_t name,
    ecritum_namespace_t *out_namespace,
    ecritum_error_t *out_error
);

int ecritum_namespace_destroy(
    ecritum_namespace_t *namespace_handle,
    ecritum_error_t *out_error
);

int ecritum_namespace_register_function(
    ecritum_namespace_t namespace_handle,
    ecritum_string_view_t name,
    ecritum_host_fn_t callback,
    void *user_data,
    ecritum_user_data_destroy_fn_t destroy_user_data,
    ecritum_function_t *out_function,
    ecritum_error_t *out_error
);

int ecritum_function_destroy(
    ecritum_function_t *function,
    ecritum_error_t *out_error
);
```

`out_namespace` and `out_function` are required and set to `0` before work
starts. Destroy with `NULL` or zero is a no-op. Successful destroy zeros caller
storage.

Namespace and function handles follow the same generational registry rules as
runtime/context/error handles. Stale, wrong-kind, destroyed, and arbitrary
invalid handles return `ECRITUM_ERROR_INVALID_HANDLE`.

Duplicate namespace names within a runtime and duplicate function names within a
namespace return a new status:

```c
#define ECRITUM_ERROR_ALREADY_EXISTS 21
```

No `NOT_FOUND` status is added in M2-004 because there is no unregister-by-name
API. Missing/stale unregister-by-handle is `ECRITUM_ERROR_INVALID_HANDLE`.

## Name Rules

Names are ASCII, copied on success, and validated before ownership transfer.
Each namespace or function name view is capped at 255 bytes before copying.
Overlong names return `ECRITUM_ERROR_INPUT_TOO_LARGE`.

Namespace segments: `[A-Za-z][A-Za-z0-9_]*`

Namespaces may contain `.` between segments. Empty segments are invalid.

Function names: `[A-Za-z][A-Za-z0-9_]*`

The first namespace segment and function names are compared case-insensitively
against reserved prefixes:

- `ecritum`
- `java`
- `javax`
- `sun`
- `graal`
- `truffle`

Reserved names return `ECRITUM_ERROR_INVALID_ARGUMENT` with redacted diagnostics.

## Ownership And Cleanup

`user_data` ownership transfers to Ecritum only after successful function
registration. Failed registration, including duplicate names, invalid names,
invalid handles, null callback, null output, and out-of-memory, never calls
`destroy_user_data`.

After successful registration, cleanup runs exactly once through the first of:

- `ecritum_function_destroy`
- `ecritum_namespace_destroy`
- `ecritum_runtime_destroy`

`destroy_user_data == NULL` is allowed and means no destructor is called.

Cleanup callbacks and future host callbacks must never execute while the global
registry mutex, runtime lock, namespace lock, context lock, job lock, or call
lock is held. Implementation collects cleanup work under lock, tombstones
handles, releases locks, then invokes destructors.

Runtime destroy rejects live contexts as ADR-002 requires. If no contexts are
live, runtime destroy also destroys live namespaces and functions exactly once.

## Swift Mapping

Swift adds:

```swift
public func namespace(_ name: EcritumNamespace.Name) throws -> EcritumNamespace

public final class EcritumNamespace {
    public struct Name: Hashable, Sendable {
        public let rawValue: String
        public init(_ rawValue: String) throws
    }

    public func register(
        _ name: EcritumFunctionName,
        _ function: @escaping EcritumHostFunction
    ) throws
}

public struct EcritumFunctionName: Hashable, Sendable {
    public let rawValue: String
    public init(_ rawValue: String) throws
}

public typealias EcritumHostFunction =
    @Sendable (EcritumCall) throws -> EcritumValue
```

`EcritumNamespace` owns the native namespace handle, retains its parent runtime,
and retains native function handles so `try namespace.register(...)` persists
even when the caller ignores a return value. `EcritumNamespace` is not
`Sendable`.

`EcritumCall` has no public initializer, no raw handle exposure, and no public
argument accessors until call/value accessors are implemented. Swift 5.9 cannot
enforce nonescaping borrowed values with ownership types, so future call
accessors must check an internal active flag and throw `.invalidHandle` after
callback return.

Synchronous callbacks run on the Ecritum worker thread that invokes them. M2
does not implicitly hop to `MainActor`.

## Verification Requirements

M2-004 implementation must add:

- pure Swift tests for namespace and function name validation
- Swift fake-adapter tests for namespace creation, registration success,
  duplicate registration, registration after close, failed registration cleanup,
  runtime close/deinit cleanup, callback success, and callback error mapping
- C fake-backend contract tests for nulls, invalid/stale/wrong-kind handles,
  invalid names, duplicate registration, `destroy_user_data == NULL`, cleanup
  exactly once on function/namespace/runtime destroy, failed registration no
  cleanup, and runtime isolation
- C test-only callback invocation under `ECRITUM_TESTING`; no public invocation
  symbol is exported or listed in the ABI manifest
- C++ public-header smoke for the new typedefs and function declarations
- ASan/UBSan for the C registration contract
- ABI manifest and XCFramework symbol checks for every public registration
  symbol

Real script-to-host callback invocation, argument accessors, value-handle
conversion, job dispatch, deadlock watchdogs, and Native Image callback smoke
are owned by eval/job implementation tasks. M2-004 must not claim scripts can
call registered functions.

## Consequences

The ABI gets namespace and function handles before eval exists, which makes host
registration lifecycle testable early. The slice avoids overclaiming callback
execution while still proving the dangerous ownership path: copied descriptors,
boxed Swift closures, duplicate rejection, and exactly-once destructor cleanup.
