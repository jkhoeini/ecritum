# ADR-002: C ABI Ownership, Lifecycle, Strings, And Errors

Status: Accepted

Reviewers: Architecture Expert, Clean Code, Tests/TDD, Security, Swift DX,
Claude CLI.

## Context

Ecritum's public integration surface is a C ABI wrapped by Swift. Other
language wrappers must be able to bind the same ABI without depending on Swift,
C++, GraalVM generated headers, Java exceptions, or JVM-specific types.

M1 exposes only `ecritum_version(char *, size_t)`. M2 adds runtime, context,
value, error, and host callback foundations. The plan previously sketched raw
opaque pointer handles and `void destroy(handle *)` functions. That is not
strong enough for the lifecycle and security requirements in PROJECT.org:
invalid handles, double destroy, use after destroy, ownership, and error
conversion must have defined behavior before implementation starts.

## Decision

The M2 public ABI uses opaque value handles backed by an internal generational
handle registry, not dereferenceable public pointers.

Public handle type names stay language-neutral:

```c
typedef uint64_t ecritum_runtime_t;
typedef uint64_t ecritum_context_t;
typedef uint64_t ecritum_value_t;
typedef uint64_t ecritum_error_t;
typedef uint64_t ecritum_call_t;
```

The bit layout is private. `0` is the invalid empty handle for every handle
type. Each nonzero handle encodes enough private registry information to
validate kind, slot, and generation before reaching any runtime object. Public C
functions must never dereference arbitrary caller-provided handle values as
pointers. A copied stale handle returns `ECRITUM_ERROR_INVALID_HANDLE`; it must
not crash and must not accidentally resolve to a newer object in a reused slot.

The current M1 `ecritum_version` caller-buffer API remains a version-only smoke
symbol. New M2 ownership rules apply to all new runtime, context, value, error,
callback, and string-copy APIs.

## Function Convention

All fallible public functions return an integer status code. `ECRITUM_OK` is
zero. Nonzero values are stable named constants and must be kept in sync across
`ecritum.h`, Java status constants, Swift mapping, and
`docs/abi/ecritum-c-abi.json`.

M2 status constants must include at least:

- `ECRITUM_OK`
- `ECRITUM_ERROR_INVALID_ARGUMENT`
- `ECRITUM_ERROR_INVALID_HANDLE`
- `ECRITUM_ERROR_BUFFER_TOO_SMALL`
- `ECRITUM_ERROR_OUT_OF_MEMORY`
- `ECRITUM_ERROR_INVALID_UTF8`
- `ECRITUM_ERROR_INPUT_TOO_LARGE`
- `ECRITUM_ERROR_INVALID_CONFIG`
- `ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION`
- `ECRITUM_ERROR_CONTEXTS_ALIVE`
- `ECRITUM_ERROR_CLOSED`
- `ECRITUM_ERROR_BUSY`
- `ECRITUM_ERROR_REENTRANT_CALL`
- `ECRITUM_ERROR_PERMISSION_DENIED`
- `ECRITUM_ERROR_TIMEOUT`
- `ECRITUM_ERROR_CANCELLED`
- `ECRITUM_ERROR_SCRIPT`
- `ECRITUM_ERROR_CALLBACK`
- `ECRITUM_ERROR_RUNTIME_UNAVAILABLE`
- `ECRITUM_ERROR_TEARDOWN_FAILED`
- `ECRITUM_ERROR_INTERNAL`

Every fallible function that can produce diagnostics accepts an optional
`ecritum_error_t *out_error`. Passing `NULL` is legal and discards diagnostics.
When `out_error` is not `NULL`, success sets `*out_error = 0`; failure sets at
most one owned error handle. Callers destroy it with
`ecritum_error_destroy(&error)`.

Create functions set output handles to `0` before doing work. On success they
write a nonzero owned handle. On failure the output handle remains `0`.
Passing a required output pointer as `NULL` returns
`ECRITUM_ERROR_INVALID_ARGUMENT`.

Destroy and free functions take a pointer to the handle they consume:

```c
int ecritum_runtime_destroy(ecritum_runtime_t *runtime, ecritum_error_t *out_error);
int ecritum_context_destroy(ecritum_context_t *context, ecritum_error_t *out_error);
int ecritum_value_destroy(ecritum_value_t *value);
int ecritum_error_destroy(ecritum_error_t *error);
```

`destroy(NULL)` and `destroy(&zero_handle)` are no-ops that return
`ECRITUM_OK`. A successful destroy sets the caller's handle storage to `0`.
This maps directly to Swift `close()` and `deinit` without leaving the wrapper
holding a non-nil stale handle.

If the caller copied a handle value before destroy, later use of the copied
value must return `ECRITUM_ERROR_INVALID_HANDLE`. Arbitrary invalid handle values
also return `ECRITUM_ERROR_INVALID_HANDLE`. Ecritum validates handles; it cannot
validate arbitrary non-null output pointer addresses supplied by C callers.

## Lifecycle State Model

Lifecycle behavior is specified as a pure state machine plus adapter effects:

```text
state + event -> new state + status + optional error + adapter effects
```

The state transition logic must be testable without GraalVM. Adapter effects are
limited to named boundaries: native allocation, handle registry updates, Graal
isolate/thread handling, host callback dispatch, logging, clock/random, IO,
network, process, environment, and Swift executor hopping.

Runtime states:

- `empty`: zero handle
- `active`: valid runtime handle
- `busy`: internal operation in progress when an operation cannot overlap
- `closing`: destroy in progress
- `poisoned`: teardown or internal failure made further use unsafe
- `destroyed`: registry tombstone; stale handles fail validation

Context states mirror runtime states but are scoped under a runtime. Contexts
cannot outlive their runtime. `ecritum_runtime_destroy` with live contexts
returns `ECRITUM_ERROR_CONTEXTS_ALIVE`, leaves the runtime valid, and fills an
error when requested. `ecritum_context_destroy` during an active operation
returns `ECRITUM_ERROR_BUSY`, leaves the context valid, and fills an error when
requested; it does not block indefinitely and does not race the active
operation. Destroy contexts before the parent runtime.

Backend teardown failure must be observable. If runtime teardown fails after the
handle can no longer be safely used, `ecritum_runtime_destroy` returns
`ECRITUM_ERROR_TEARDOWN_FAILED`, sets the caller's handle to `0`, tombstones the
registry entry, and reports diagnostics in `out_error` when requested. Teardown
failure must not be silently hidden behind a prior success or failure status.
Unexpected internal failures may poison a context independently of the runtime
when the runtime remains safe. A poisoned context rejects future operations with
the original status category where possible, or `ECRITUM_ERROR_INTERNAL` when no
more precise status is safe.

The state model must cover create success, create failure, active use, close,
destroy, double destroy, copied stale handle, wrong-kind handle, null output
pointers, context-outlives-runtime attempts, and teardown failure.

## Threading And Reentrancy

Thread-safety is part of each handle's contract:

- Runtime handles are internally synchronized and may be used from multiple
  threads unless a later ADR narrows a specific function.
- Context handles allow one active operation at a time. Overlapping operations
  return `ECRITUM_ERROR_BUSY`; they do not race and do not block indefinitely.
- Value and error handles are immutable after creation. Accessors are
  side-effect-free and do not trigger guest execution, lazy `toString`, IO,
  callbacks, or runtime mutation. Concurrent accessor calls on the same value or
  error handle are safe.
- `ecritum_call_t` is a borrowed callback-scope handle. It is valid only while
  the host callback is executing, must not be stored, and is never destroyed by
  the host. Use after the callback returns is a stale-handle error and returns
  `ECRITUM_ERROR_INVALID_HANDLE`.
- Same-context reentrant eval or destroy from a host callback returns
  `ECRITUM_ERROR_REENTRANT_CALL` until ADR-003 defines async/job behavior.

Do not mark Swift runtime or context wrappers as `Sendable` unless the C handle
function they wrap is documented thread-safe or Swift serializes access
internally with a lock, actor, or queue and tests prove the behavior.

## Strings And Input Buffers

New M2 APIs do not use unbounded bare C strings for script source, config JSON,
options JSON, or returned guest strings. They use explicit length-carrying
views:

```c
typedef struct {
    const uint8_t *data;
    size_t len;
} ecritum_bytes_t;

typedef struct {
    const char *data;
    size_t len;
} ecritum_string_view_t;
```

These structs use the platform C ABI's natural alignment and packing. Ecritum
does not apply custom packing pragmas to public ABI structs.

Input views are borrowed for the duration of the call. Ecritum never retains a
caller-owned input pointer unless the function explicitly documents a copy.
Inputs that are semantically UTF-8 must be validated and return
`ECRITUM_ERROR_INVALID_UTF8` on failure. Oversized input returns
`ECRITUM_ERROR_INPUT_TOO_LARGE`; each API must document its limit.

Borrowed output string views are valid until the owning handle is destroyed or
until the next documented mutating operation on that owner. Borrowed views must
not be freed by callers and may contain embedded NUL bytes.

Owned string copies, when needed, use an explicit owned buffer type and
`ecritum_string_free(&owned_string)`. `ecritum_string_free(NULL)` and freeing an
empty owned string are no-ops. The free operation sets the caller storage to an
empty value.

Swift wrappers must eagerly copy borrowed C string/data views into Swift values
before destroying the owning C handle or returning to user code.

## Error Model

Status codes are control flow. `ecritum_error_t` is diagnostics.

An error object is immutable, owned by the caller, and destroyed with
`ecritum_error_destroy(&error)`. Accessors return status values and borrowed
views valid until the error is destroyed.

M2 error diagnostics must support:

- status code
- stable machine-readable category
- redacted user-facing message
- operation name
- language, when relevant
- source name, line, and column, when relevant
- stack frame count and borrowed stack frame views, when relevant
- optional debug diagnostics only when an explicit diagnostic capability enables
  them

By default, error messages must not expose host filesystem paths, raw Java
`Throwable` class names, internal Graal stack traces, environment values,
process commands, or guest source text beyond the source name and location. Raw
diagnostics are opt-in and never part of the default error surface.

Every public C entry point and every host callback adapter catches Java
`Throwable`, C++ exceptions if any are introduced, Swift errors crossing wrapper
adapters, and internal failures before returning to C. No exception or panic may
cross the FFI boundary. If an unexpected `Throwable` escapes an internal Graal
operation, the entry point maps it to status plus error and marks the affected
handle poisoned when further use is unsafe.

The current Swift `EcritumError.runtimeCallFailed(status:)` is M1 scaffolding.
M2 Swift errors must be structured, typed, `Sendable`, and at least capable of
representing missing runtime artifact, invalid argument, invalid handle, closed
or use-after-close, runtime unavailable, permission denied, timeout, cancelled,
script error, callback error, internal error, and unknown status.

## Config And Capability Safety

`config_json` and `options_json` inputs are Ecritum schemas, not raw GraalVM or
Polyglot option passthroughs. Schemas are versioned. Unknown keys fail closed.
Defaults deny filesystem, network, process execution, environment access,
reflection, class loading, native library loading, unrestricted Java lookup, and
raw host access. Permission policy shape is finalized in ADR-016 and the
sandbox threat model in ADR-004, but ADR-002 requires the ABI to reject
unrecognized or dangerous raw options from the start.

## Host Callback Boundary

Host callback registration is implemented later, but ADR-002 fixes the
ownership contract now.

`user_data` ownership transfers to Ecritum only after successful registration.
If registration fails, the host retains ownership and Ecritum must not call
`destroy_user_data`. After successful registration, `destroy_user_data` runs
exactly once during explicit unregister or runtime destroy.

Callbacks receive a borrowed `ecritum_call_t` valid only for the callback
duration. Callback result and error output handles are initialized to `0` before
the callback. On callback success, ownership of the returned value transfers to
Ecritum. On callback failure, ownership of the returned error transfers to
Ecritum. Callback adapters convert host errors/exceptions into
`ECRITUM_ERROR_CALLBACK` plus structured diagnostics. Callback executor hopping
and async behavior are finalized in ADR-003 and ADR-016; until then callbacks
run synchronously on the eval caller path and reentrant same-context eval is
rejected.

## Planned Symbol Matrix

M2 implementation must update this matrix into `docs/abi/ecritum-c-abi.json`
before exporting the symbols.

The planned synchronous eval shape is:

```c
int ecritum_eval(
    ecritum_context_t context,
    ecritum_string_view_t language,
    ecritum_bytes_t source,
    ecritum_string_view_t source_name,
    ecritum_bytes_t options_json,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error
);
```

| Symbol | Ownership | Null handling | Error handling | Required tests |
| --- | --- | --- | --- | --- |
| `ecritum_runtime_create(config, out_runtime, out_error)` | returns owned runtime handle | null `out_runtime` invalid; empty `config` view means default config | optional owned error on failure | success, invalid config, unsupported config version, null out, null error, out-of-memory |
| `ecritum_runtime_destroy(runtime, out_error)` | consumes runtime handle and zeros caller storage | null pointer/zero no-op | live contexts leave handle valid; teardown failure observable | null, zero, active destroy, live contexts, copied stale handle, teardown failure, repeated cycles |
| `ecritum_context_create(runtime, config, out_context, out_error)` | returns owned context handle | invalid/null runtime rejected; null out invalid | optional owned error on failure | null runtime, destroyed runtime, invalid config, parent lifetime |
| `ecritum_context_destroy(context, out_error)` | consumes context handle and zeros caller storage | null pointer/zero no-op | optional owned error on failure | null, zero, active destroy, copied stale handle, double destroy |
| `ecritum_value_destroy(value)` | consumes owned immutable value handle | null pointer/zero no-op | no diagnostic object | null, zero, copied stale handle, wrong-kind handle |
| `ecritum_error_destroy(error)` | consumes owned immutable error handle | null pointer/zero no-op | no diagnostic object | null, zero, double destroy through same storage, copied stale handle |
| `ecritum_error_*` accessors | borrowed views from error | null/invalid error returns invalid handle | no new error allocation | code/message/source/stack access, destroyed error |
| `ecritum_string_free(string)` | consumes owned string buffer | null/empty no-op | no diagnostic object | null, empty, UTF-8 round trip, embedded NUL |
| `ecritum_eval(context, language, source, source_name, options_json, out_result, out_error)` | returns owned value or error | null/invalid context/source/options defined; empty `source_name` view means anonymous source; empty `options_json` view means default options | optional owned error on failure | nulls, invalid UTF-8, oversized input, script error, busy context, permission denied |
| host callback functions | result/error ownership transfers at callback return | borrowed call cannot be stored | callback errors converted | success, callback error, user_data cleanup exactly once, reentrant call rejected |

## Swift Mapping

Swift wrapper types own C handles privately. Raw `ecritum_*_t` values are not
exposed in public Swift APIs.

Swift `close()` is idempotent, serializes with in-flight work, calls the
pointer-to-handle C destroy function, and nils the stored handle. `deinit` calls
`close()` if the handle is still open. Post-close operations throw a typed
closed/use-after-close error; they do not trap.

`EcritumContext` must keep its parent `EcritumRuntime` alive or explicitly
reject parent close while contexts are live. Swift values returned to user code
are copied into Swift-native `EcritumValue` cases before C owner handles are
destroyed. Swift tests use a fake C ABI adapter seam for deterministic
lifecycle and error paths that cannot be triggered reliably through GraalVM.

## Verification Requirements

No M2 ABI function is accepted without tests for success, null arguments,
ownership, lifecycle failure, and Swift error mapping.

Required gates before M2 lifecycle/value work is complete:

- ABI manifest covers every public symbol, nullable parameter, ownership rule,
  success output, error output, and expected status code.
- `just check-abi` rejects undeclared public symbols and private Graal symbols
  in the public framework.
- Every status code is provoked from C and mapped in Swift.
- Public header compiles as C and C++.
- C lifecycle contract tests cover runtime/context create/destroy, nulls,
  wrong-kind handles, copied stale handles, live contexts, teardown failure,
  error ownership, and string ownership.
- Swift lifecycle tests cover `close()`, `deinit`, post-close errors, parent
  runtime lifetime, structured error mapping, and C error/string cleanup.
- Java tests prove status-code parity and catch-all no-throw entrypoint
  behavior.
- `just test-c-abi-asan` runs a fake-backend C harness with ASan/UBSan.
- Lifecycle stress performs at least 1,000 create/context/destroy cycles under
  sanitizer or the closest macOS-supported equivalent.
- A real-framework leak smoke uses `leaks(1)` or equivalent where available.
- TSan is required before claiming any handle operation is thread-safe,
  including runtime operations and value/error accessors. Overlapping context
  operations return `ECRITUM_ERROR_BUSY` and are tested unless a later ADR
  widens the context concurrency contract.
- `mise exec -- just test` or a documented CI rollup runs Swift tests, Java
  tests, ABI checks, C contract tests, and sanitizer/leak gates before the M2
  milestone is accepted.

## Rejected Alternatives

Raw opaque pointer handles were rejected. They are convenient, but arbitrary
invalid pointers cannot be validated without dereferencing them, and copied
stale pointers can become use-after-free bugs or accidentally point to a newer
object at a reused address.

`void destroy(handle *)` was rejected. It cannot report live-child or teardown
failures and leaves Swift wrappers with more manual state bookkeeping.

Bare NUL-terminated C strings for scripts/config/options were rejected. Guest
strings and source buffers may contain embedded NUL bytes, and unbounded input
is not acceptable for untrusted scripting.

Raw GraalVM/Polyglot option passthrough was rejected. Ecritum's ABI owns a
versioned, deny-by-default policy schema.

## Consequences

The ABI is slightly more explicit than a raw pointer API, but it is safer for
C, Swift, and future wrappers. Implementation must build a generational handle
registry before runtime/context/value code lands. Swift wrappers get simple
single-destroy RAII. Security tests can exercise invalid and stale handles
without depending on process crashes. The ABI manifest becomes the authority for
status-code parity and symbol ownership as the public surface grows.
