# ADR-021: Value, Call, And Eval ABI Surface

Status: Accepted

Reviewers: Architecture Expert, Clean Code, Security, Tests/TDD, GraalVM
Runtime, Claude CLI.

## Context

ADR-002 reserved `ecritum_value_t` and `ecritum_call_t` and required immutable
values, explicit ownership, and borrowed callback-scope calls. ADR-003 defined
the async `ecritum_job_t` state machine and `ecritum_eval_start` path. ADR-019
implemented host function registration but intentionally deferred argument
accessors, result conversion, and real script-to-host invocation.

M3-002 cannot implement SCI eval or host calls until the public value and call
surface is concrete. That surface must stay language-neutral so Clojure,
JavaScript, Lua, Python, Ruby, Swift, and future wrappers all use the same
handles.

## Decision

M3-002A completes the language-neutral public ABI for values, callback
arguments, and async eval jobs. It does not add any Clojure-specific symbol.

Public value kinds are stable integer constants:

```c
#define ECRITUM_VALUE_KIND_NULL 0
#define ECRITUM_VALUE_KIND_BOOL 1
#define ECRITUM_VALUE_KIND_INT 2
#define ECRITUM_VALUE_KIND_DOUBLE 3
#define ECRITUM_VALUE_KIND_STRING 4
#define ECRITUM_VALUE_KIND_DATA 5
#define ECRITUM_VALUE_KIND_ARRAY 6
#define ECRITUM_VALUE_KIND_OBJECT 7
```

Object construction uses string-keyed entries:

```c
typedef struct {
    ecritum_string_view_t key;
    ecritum_value_t value;
} ecritum_object_entry_t;
```

Value creation APIs return owned immutable value handles. Array and object
creation deep-copy input child values, so the caller keeps ownership of the
input handles and remains responsible for destroying them.

Arrays and objects are created atomically in M3-002A. Incremental mutable
builders are rejected for the public ABI because they would add partially built
public objects, rollback rules, and mutating value handles before a language
adapter needs them.

```c
int ecritum_value_make_null(ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_bool(int value, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_int(int64_t value, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_double(double value, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_string(ecritum_string_view_t value, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_data(ecritum_bytes_t value, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_array(const ecritum_value_t *items, size_t count, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_make_object(const ecritum_object_entry_t *entries, size_t count, ecritum_value_t *out_value, ecritum_error_t *out_error);
int ecritum_value_destroy(ecritum_value_t *value);
```

Scalar accessors borrow immutable storage from the value handle. The borrowed
string/data views stay valid until the owning value handle is destroyed.

```c
int ecritum_value_kind(ecritum_value_t value, int *out_kind);
int ecritum_value_get_bool(ecritum_value_t value, int *out_value);
int ecritum_value_get_int(ecritum_value_t value, int64_t *out_value);
int ecritum_value_get_double(ecritum_value_t value, double *out_value);
int ecritum_value_get_string(ecritum_value_t value, ecritum_string_view_t *out_value);
int ecritum_value_get_data(ecritum_value_t value, ecritum_bytes_t *out_value);
int ecritum_value_count(ecritum_value_t value, size_t *out_count);
```

Nested accessors return owned child copies. Callers destroy those returned
values with `ecritum_value_destroy`.

```c
int ecritum_value_array_get(ecritum_value_t value, size_t index, ecritum_value_t *out_item, ecritum_error_t *out_error);
int ecritum_value_object_entry(ecritum_value_t value, size_t index, ecritum_string_view_t *out_key, ecritum_value_t *out_value, ecritum_error_t *out_error);
```

Wrong-kind access, null required outputs, invalid indices, duplicate object
keys, invalid UTF-8 string/object keys, stale handles, and wrong-kind handles
return existing status codes. M3-002A does not add a type-mismatch or
out-of-range status; those cases use `ECRITUM_ERROR_INVALID_ARGUMENT` with
structured diagnostics when an `out_error` parameter exists.

Value handles share the same generational registry as other public handles in
M3-002A. Registry exhaustion returns `ECRITUM_ERROR_OUT_OF_MEMORY`; it never
silently drops children or aliases a stale handle. A separate value arena may be
introduced later behind the same public handles if value-heavy workloads make
the registry cap a product problem.

Callback arguments are exposed only through borrowed `ecritum_call_t` handles
valid during callback execution. Accessors return owned value copies so host
callbacks cannot mutate guest-owned argument storage.

```c
int ecritum_call_argument_count(ecritum_call_t call, size_t *out_count, ecritum_error_t *out_error);
int ecritum_call_argument(ecritum_call_t call, size_t index, ecritum_value_t *out_argument, ecritum_error_t *out_error);
```

Function records are pinned while a callback is active. Destroying a function,
namespace, or runtime while a callback is active must not run
`destroy_user_data` until the callback returns. M3-002A may implement this by
returning `ECRITUM_ERROR_BUSY` from explicit destroy paths or by marking cleanup
pending and running it after the active callback count reaches zero. Either
implementation must prove that `callback`, `user_data`, call arguments, and call
handles remain valid for the callback duration and become invalid immediately
after callback cleanup.

Callback result precedence is:

- if the callback returns `ECRITUM_OK`, `out_result` must contain one owned
  value handle and `out_error` must be `0`
- if the callback returns a non-OK status or throws through the Swift adapter,
  Ecritum destroys any returned value, consumes/destroys any returned callback
  error after copying safe diagnostics, and the guest job reports
  `ECRITUM_ERROR_CALLBACK`
- if the callback returns `ECRITUM_OK` with no result, Ecritum treats the result
  as `nil`/`EcritumValue.null`
- if both result and error are returned, error wins and the result is destroyed

M3-002A implements the ADR-003 async eval symbols exactly as the public eval
surface. Synchronous eval is not a public M3-002A symbol.

```c
typedef uint64_t ecritum_job_t;

int ecritum_eval_start(
    ecritum_context_t context,
    ecritum_string_view_t language,
    ecritum_bytes_t source,
    ecritum_string_view_t source_name,
    ecritum_bytes_t options_json,
    ecritum_job_t *out_job,
    ecritum_error_t *out_error
);
```

Job polling, waiting, cancellation, result, and destroy follow ADR-003 without
renaming or Clojure-specific behavior.

M3-002B extends the general error diagnostic surface so script errors can expose
structured language and source-name fields without parsing message text:

```c
int ecritum_error_language(ecritum_error_t error, ecritum_string_view_t *out_language);
int ecritum_error_source_name(ecritum_error_t error, ecritum_string_view_t *out_source_name);
```

These are language-neutral borrowed-view accessors. They do not add
Clojure-specific public ABI symbols.

M3-002C projects registered host functions into SCI without adding public ABI.
The projected Clojure syntax is ordinary namespace-qualified Clojure function
syntax: a registered Ecritum namespace `app` with function `answer` is callable
as `(app/answer)`, and `app.tools` with function `format` is callable as
`(app.tools/format)`. Unqualified host functions are not installed.

The Java/C bridge for host projection is private to the packaged runtime. The C
wrapper snapshots registered namespace/function names for the runtime before a
production Clojure eval enters SCI, passes that manifest plus a private C
function pointer and opaque runtime handle to Native Image, and keeps public
`ecritum_*` ABI unchanged. Java installs only the names in that manifest as SCI
functions; no raw Java host object, Swift closure, C handle, Polyglot value, or
runtime registry object is visible to script code.

Projection lifetime is pinned for the active eval. A function projected into an
active SCI eval must not be destroyed until the eval worker releases the
projection, even before the function is actually called. Explicit
`ecritum_function_destroy`, `ecritum_namespace_destroy`, and
`ecritum_runtime_destroy` therefore return `ECRITUM_ERROR_BUSY` or
`ECRITUM_ERROR_REENTRANT_CALL` as appropriate while projected functions or
callbacks are active. The implementation may use a separate active-projection
count or a shared internal active-use count, but tests must prove callback
closures, `user_data`, and function records remain valid for the whole eval.

Host callbacks invoked from SCI run synchronously on the eval worker thread.
They must use the same callback stack and active-call accounting as the existing
C callback contract so same-runtime eval/job/context/runtime reentry remains
denied. M3-002C does not claim robust interruption of a callback that blocks
inside host code; timeout/cancellation may request cancellation and must publish
a terminal state only after the callback returns unless a later ADR adds a safe
host-callback interruption mechanism.

Host-call arguments and results use the same backend value wire format as eval
results. M3-002C must include all public value kinds, including
`ECRITUM_VALUE_KIND_DATA`, or explicitly leave a failing/blocked test and debt
entry for any unsupported value kind. Callback failures map to
`ECRITUM_ERROR_CALLBACK` with operation `eval`, language `clojure`, the original
source name, category `callback`, and redacted diagnostics.

The M3-002A test-mode evaluator is only compiled under `ECRITUM_TESTING`. It
must not be packaged, listed as a language provider, or used as conformance
evidence for real `eval`, `host_call`, `script_error`, timeout, or permission
capabilities. Required conformance cases for real language eval remain pending
until M3-002B and M3-002C wire SCI and host projection.

## Swift Mapping

Swift implements ADR-016's `EcritumScript` and
`EcritumContext.eval(_:) async throws -> EcritumValue`.

The Swift C adapter starts a native job, waits in finite intervals, drains the
single result, copies it into Swift-native `EcritumValue`, destroys the native
value handle, and destroys the job. The raw `ecritum_job_t`, `ecritum_value_t`,
and `ecritum_call_t` handles remain private.

`EcritumCall` gains value-based argument accessors such as
`value(at:)`. Convenience typed accessors may be added later as pure Swift sugar
over the same value accessor.

Host callback results are converted from Swift `EcritumValue` into owned C
value handles before returning to the C callback adapter. Callback errors return
`ECRITUM_ERROR_CALLBACK` and never pass Swift errors across the C ABI.

## Verification Requirements

M3-002A must add:

- C contract tests for value creation, scalar access, array/object child copies,
  invalid/stale/wrong-kind handles, null outputs, wrong-kind access, duplicate
  object keys, and destroy idempotence.
- C contract tests for eval start, poll/wait, single-drain result, busy context,
  cancel idempotence, `UINT64_MAX` wait rejection, active destroy rejection,
  terminal job destroy, undrained-result destroy, single-drain error ownership,
  stale/wrong-kind/null job handles, and invalid job handles.
- C contract tests for callback argument access, use-after-callback invalidation,
  active-callback cleanup pinning, callback result propagation, callback
  result/error precedence, and returned-value cleanup for non-OK callback
  statuses.
- C contract tests for call-handle abuse: zero, arbitrary, stale,
  wrong-kind, null output, out-of-range argument index, and post-callback
  invalidation for `ecritum_call_argument_count` and `ecritum_call_argument`.
- Swift fake-adapter tests for `EcritumScript`, async eval result/error mapping,
  finite job wait/result/destroy sequencing, timeout/cancel mapping, Swift task
  cancellation calling native cancel, no public job handle exposure, closed
  context behavior, callback result conversion, and `EcritumCall.value(at:)`.
- ABI manifest and C++ header smoke updates for every new public typedef,
  struct, constant, and symbol.
- ASan/UBSan coverage for the new C value/job/call contract tests.
- `justfile` targets `test-c-abi-eval` and `test-c-abi-eval-asan`, and
  `mise exec -- just check-abi` coverage for the new public symbols.
- Conformance allocation/release evidence includes the new eval/value/job C
  contract tests while real eval/host conformance cases remain pending until
  M3-002B/C.

M3-002A may use a test-mode fixture evaluator or an unsupported-language
terminal job while M3-002B adds SCI. It must not claim Clojure support until the
SCI backend, strict conformance provider, security abuse cases, Native Image
build, and release gates pass.

## Consequences

Returning owned child copies costs extra allocation when iterating arrays and
objects, but it avoids borrowed child-handle lifetime ambiguity and keeps every
public `ecritum_value_t` either owned by the caller or owned internally.

The ABI is larger than a JSON-only result channel, but it preserves typed data,
binary blobs, stable ownership, and host callback symmetry for every language
runtime.
