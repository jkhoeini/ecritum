# ADR-003: Runtime/Context Concurrency And Async Job Model

Status: Accepted

Reviewers: Architecture Expert, Security, Tests/TDD, Swift DX, GraalVM Runtime,
Claude CLI.

## Context

ADR-002 defines the C ABI handle, ownership, lifecycle, threading, string, and
error rules. ADR-016 defines the first-party Swift shape and sketches
`EcritumContext.eval(_:) async throws -> EcritumValue`.

M2-004 host function registration and M3 eval work need the missing concurrency
contract before implementation starts. Without it, callback reentrancy, timeout
behavior, cancellation, close during active work, Swift executor hopping, and
GraalVM thread attachment would be left to ad hoc implementation choices.

The current lifecycle implementation creates runtime and context handles only.
It intentionally defers eval, callbacks, async jobs, cancellation, and timeout.

## Decision

Async eval is the primary native model. Synchronous eval, if exposed, is sugar
over the same async job state machine and must not introduce a separate
unbounded blocking path.

M2 adds an owned job handle:

```c
typedef uint64_t ecritum_job_t;
```

`ecritum_job_t` follows ADR-002 handle rules. It is an opaque generational
handle backed by the internal registry. `0` is invalid. Arbitrary, stale,
destroyed, and wrong-kind handles return `ECRITUM_ERROR_INVALID_HANDLE`.

Each job is scoped under exactly one context. A context may have at most one
live job in M2. Starting another job on the same context before the previous
job is terminal and destroyed returns `ECRITUM_ERROR_BUSY`; it does not queue,
race, or block.

Runtime operations may be internally synchronized, but Ecritum makes no public
parallel-job claim in M2. Parallel jobs across different contexts require a
later implementation task with stress tests and TSan before any thread-safety
claim is made.

## Execution Thread Model

Async jobs run on Ecritum-owned worker threads associated with the runtime. They
do not run on the caller thread, the Swift cooperative executor, or `MainActor`.

`ecritum_eval_start` validates and copies all data needed for asynchronous
execution before returning. `language`, `source`, `source_name`, and
`options_json` are borrowed only for the duration of the `eval_start` call.
After `eval_start` returns, the caller may free or mutate its input buffers
without affecting the job.

Worker concurrency is bounded by runtime configuration. The M2 default is
conservative: one active job per context, and no public guarantee of parallel
execution across contexts until implementation proves it. If the worker queue is
full or the context already has a live job, `eval_start` returns
`ECRITUM_ERROR_BUSY`.

Job control functions are internally synchronized and may be called from any
native thread while the job handle is valid:

- `job_poll`
- `job_wait`
- `job_cancel`
- `job_result`
- `job_destroy`

This C-level synchronization does not make Swift wrappers `Sendable`. Swift
`Sendable` requires separate wrapper synchronization and TSan evidence.

## GraalVM Thread Attachment

Public handles never store `graal_isolatethread_t`. A worker thread attaches to
the runtime isolate only while entering the Graal/backend adapter for a job, and
detaches before that worker returns to idle state or exits.

Public control calls such as `job_poll`, `job_cancel`, `job_wait`,
`job_result`, and `job_destroy` attach only if they need to enter backend code.
Pure registry/state transitions do not attach to Graal.

Synchronous host callbacks run on the Ecritum worker thread that is executing
the guest job. The callback returns on that same native call stack. M2 has no
async callback return path and no callback continuation that can resume on a
different native thread.

## Job States

M2 defines stable public job state constants:

```c
#define ECRITUM_JOB_PENDING 0
#define ECRITUM_JOB_RUNNING 1
#define ECRITUM_JOB_SUCCEEDED 2
#define ECRITUM_JOB_FAILED 3
#define ECRITUM_JOB_CANCEL_REQUESTED 4
#define ECRITUM_JOB_CANCELLED 5
#define ECRITUM_JOB_TIMED_OUT 6
#define ECRITUM_JOB_POISONED 7
```

Terminal states are `SUCCEEDED`, `FAILED`, `CANCELLED`, `TIMED_OUT`, and
`POISONED`. `PENDING`, `RUNNING`, and `CANCEL_REQUESTED` are nonterminal.

The state machine is:

```text
pending -> running | cancel_requested
running -> succeeded | failed | cancel_requested | timed_out | poisoned
cancel_requested -> cancelled | timed_out | failed | poisoned
terminal -> destroyed
```

`FAILED` means guest script failure, host callback failure, permission denial,
invalid input discovered during execution, or another recoverable terminal
error. `POISONED` means the job ended in a way that made its context, or
possibly runtime, unsafe to reuse.

## C ABI Shape

M2 async eval starts a job:

```c
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

`out_job` is required and is set to `0` before work starts. On success, it
receives a nonzero owned job handle. On failure, it remains `0`. `options_json`
uses Ecritum's versioned schema. Unknown raw backend options fail closed.

Jobs are observed and controlled through these functions:

```c
int ecritum_job_poll(
    ecritum_job_t job,
    int *out_state,
    ecritum_error_t *out_error
);

int ecritum_job_wait(
    ecritum_job_t job,
    uint64_t wait_timeout_nanos,
    int *out_state,
    ecritum_error_t *out_error
);

int ecritum_job_cancel(
    ecritum_job_t job,
    ecritum_error_t *out_error
);

int ecritum_job_result(
    ecritum_job_t job,
    ecritum_value_t *out_result,
    ecritum_error_t *out_error
);

int ecritum_job_destroy(
    ecritum_job_t *job,
    ecritum_error_t *out_error
);
```

`out_state` is required for `poll` and `wait`. `poll` never blocks. `wait`
blocks for at most `wait_timeout_nanos` on a monotonic clock. A zero wait
timeout is equivalent to `poll`. M2 does not define an infinite wait sentinel.
`UINT64_MAX` is reserved and returns `ECRITUM_ERROR_INVALID_ARGUMENT` until a
later ADR defines a sentinel. Callers that want longer waits call `wait`
repeatedly with finite timeouts.

`poll` and `wait` return `ECRITUM_OK` whenever the job handle is valid and
`out_state` is written, even when the written state is nonterminal. A wait
budget expiring while the job is still running is normal observation, not an
error status. Execution timeout is different and is represented by terminal
state plus `ECRITUM_ERROR_TIMEOUT` from `job_result`.

`job_cancel` is idempotent. Cancelling an already terminal job returns
`ECRITUM_OK` and leaves the terminal state unchanged. Cancelling a pending or
running job requests cancellation and returns `ECRITUM_OK` once the request is
recorded; it does not mean execution has already stopped. Cancellation records
`ECRITUM_JOB_CANCEL_REQUESTED` before attempting backend cancellation. If the
job is still pending, the worker must observe the request and complete it as
cancelled without entering guest execution. Pollers are not guaranteed to
observe that transitory state if the job reaches a terminal state immediately
after the request is recorded.

`job_result` is single-drain:

- `out_result` is required and is set to `0` before work starts.
- On success, it transfers one owned `ecritum_value_t` into `out_result` and
  returns `ECRITUM_OK`.
- On failure, cancellation, timeout, or poisoning, it transfers one owned
  `ecritum_error_t` into `out_error` when requested, leaves `out_result` as
  `0`, and returns the terminal status.
- If the job is not terminal, it returns `ECRITUM_ERROR_BUSY`.
- After the terminal result or error has been drained once, another
  `job_result` call returns `ECRITUM_ERROR_CLOSED`.

`job_destroy(NULL)` and `job_destroy(&zero)` are no-ops. Destroying an active
nonterminal job returns `ECRITUM_ERROR_BUSY` and leaves the handle valid.
Destroying a terminal job frees any undrained result or error, tombstones the
job handle, zeros caller storage, and releases the parent context for another
job.

Synchronous eval, if retained, has this meaning:

```text
eval_start -> repeated finite job_wait -> job_result -> job_destroy
```

It must use the same execution deadline and cancellation rules as async jobs.
It must not bypass the job registry or context busy state.

## Timeout Semantics

`job_wait` timeout and execution timeout are different.

`job_wait(..., wait_timeout_nanos, ...)` is a caller wait budget. If the wait
budget expires while the job is still running, `job_wait` returns `ECRITUM_OK`
and writes the current nonterminal state. The job continues unless it has an
execution deadline or cancellation request.

Execution timeout is configured through Ecritum eval options or inherited
context/resource limits. When the execution deadline expires, the job enters a
terminal timeout path and `job_result` returns `ECRITUM_ERROR_TIMEOUT` with
diagnostics. ADR-014 defines recovery and poisoning rules.

## Callback Reentrancy

Host callbacks are synchronous in M2. Async host callbacks remain deferred.

Callbacks receive a borrowed `ecritum_call_t` only for the callback duration, as
defined by ADR-002. Callback execution must never occur while Ecritum holds the
global registry mutex, a runtime lock, a context lock, or a job lock.

M2 rejects blocking same-runtime reentry from callbacks:

- eval/start job on any context in the same runtime from a callback returns
  `ECRITUM_ERROR_REENTRANT_CALL`
- waiting on a job in the same runtime from a callback returns
  `ECRITUM_ERROR_REENTRANT_CALL`
- destroying the originating context or runtime from a callback returns
  `ECRITUM_ERROR_REENTRANT_CALL`

This is stricter than same-context-only rejection. Cross-context callback cycles
are hard to detect reliably, so M2 rejects same-runtime blocking reentry instead
of attempting partial cycle detection. Calls into a separate runtime may be
allowed only when no locks are held and tests prove no deadlock.

Callback failures become terminal job failures with
`ECRITUM_ERROR_CALLBACK`. Callback result and error ownership follow ADR-002.

## Locking And Deadlock Rules

No public call may wait while holding the registry mutex or while holding a lock
needed by callback dispatch.

When multiple internal locks are needed, the lock order is:

```text
runtime -> context -> job -> call
```

Implementation tasks may choose finer-grained locks or actors, but they must
preserve the same acyclic order. Any lock-order change requires a focused
deadlock review and tests.

Callbacks are invoked outside internal locks. Before invoking a callback,
Ecritum records the callback-scope call handle and releases internal locks.
After the callback returns, Ecritum re-enters the locks, validates that the job
and context are still in the expected state, consumes the callback result or
error, and continues.

## Swift Mapping

The first public Swift API remains:

```swift
public func eval(_ script: EcritumScript) async throws -> EcritumValue
```

The Swift wrapper may use C job handles internally, but it does not expose
`ecritum_job_t` in public Swift API in M2.

Swift `eval` must not block a Swift cooperative executor on a native wait. The
wrapper uses a dedicated Ecritum worker/executor bridge plus continuations.
Swift task cancellation uses `withTaskCancellationHandler` and calls the native
cancel path. Cancellation resumes the Swift task only after native terminal
cleanup or after the native layer reports that the context is poisoned/busy
according to ADR-014.

Timeouts are configured as Swift `Duration` values that serialize into Ecritum
options/resource limits. A wrapper-side `Task.sleep` race is not sufficient for
execution timeout because it would leave native execution running without an
owned cancellation/recovery path.

`EcritumRuntime`, `EcritumContext`, `EcritumNamespace`, any Swift job wrapper,
and `EcritumCall` remain non-`Sendable` in M2. Only value, configuration, and
error value types may be `Sendable`. Adding `Sendable` or `@unchecked Sendable`
requires explicit synchronization and TSan evidence.

Synchronous host callbacks do not implicitly hop to `MainActor`. Hosts that need
UI work must enqueue that work themselves or wait for a future async callback
ADR/API.

## Verification Requirements

Implementation tasks that export async eval or job symbols must add:

- pure job state-machine tests for every transition
- C ABI fake-backend contract tests for nulls, invalid/stale/wrong-kind handles,
  double destroy, busy context, job wait timeout, execution timeout,
  cancellation, single-drain result, destroy with undrained result, and destroy
  while active
- Swift fake-adapter tests for async eval success, thrown script error, task
  cancellation, timeout serialization, executor bridge behavior, callback
  failure, close/deinit during active jobs, and no `Sendable` claims
- Java/Graal tests for catch-all Throwable mapping and backend terminal status
  mapping
- Native Image smoke tests for finite eval, infinite-loop timeout, cancellation,
  recursion overflow, large allocation, callback failure, callback reentrancy,
  and post-failure reuse/poisoning behavior
- deadlock watchdog tests for callback waiting on the originating job, close
  from callback, synchronous hop to the same Swift executor, concurrent
  cancel/wait/destroy, and no wait while holding locks
- ASan/UBSan C contract runs, real-framework leak smoke, and TSan before any
  public thread-safety or `Sendable` claim
- ABI manifest and public-header checks for every job state and job symbol

`release-check` must include async ABI symbols, timeout/cancel/teardown smoke,
leak/sanitizer evidence, and `bench-first-eval` once eval exists.

## Rejected Alternatives

Blocking synchronous eval as the primary ABI was rejected. It makes Swift task
cancellation, timeout, teardown, and callback deadlock behavior too easy to get
wrong.

Queuing multiple jobs per context was rejected for M2. It hides backpressure,
complicates cancellation order, and weakens the simple context state model in
ADR-002.

Exposing C job handles directly in first-party Swift was rejected for M2. Swift
developers should start with `async throws`; lower-level job controls can be
added later if real hosts need them.

Detecting all callback dependency cycles dynamically was rejected for M2.
Same-runtime blocking reentry rejection is simpler, safer, and testable.

## Consequences

The ABI becomes more explicit, but async behavior is testable at the C layer and
maps cleanly to Swift `async throws`. Context concurrency stays conservative.
Timeout and cancellation semantics have one state machine instead of separate
C, Java, and Swift interpretations. Host callback registration can proceed only
after this model is accepted.
