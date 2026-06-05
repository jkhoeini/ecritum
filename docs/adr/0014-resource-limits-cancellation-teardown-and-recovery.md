# ADR-014: Resource Limits, Cancellation, Teardown, And Recovery

Status: Accepted

Reviewers: Architecture Expert, Security, Tests/TDD, Swift DX, GraalVM Runtime,
Claude CLI.

## Context

ADR-003 defines the async job model. ADR-002 requires explicit lifecycle,
teardown, error, and callback ownership semantics. ADR-016 defines Swift
configuration and resource-limit policy as value objects that serialize into
Ecritum's deny-by-default C configuration schema.

Ecritum will run guest scripts inside the host app process. Some guest code may
loop forever, recurse until stack failure, allocate aggressively, block inside a
host callback, or trigger backend failures. The project must not promise hard
interruption or safe reuse after failure until a language backend proves it.

## Decision

Ecritum separates four concepts:

- execution deadline: a job-level monotonic deadline for guest execution
- wait timeout: a caller wait budget for `ecritum_job_wait`
- cancellation request: an idempotent request to stop a job
- recovery state: whether the context/runtime can safely accept more work after
  timeout, cancellation, callback failure, or backend failure

Execution deadlines and cancellation are native runtime concerns. Swift
wrapper-side sleeps or task cancellation alone are not sufficient because they
can leave guest execution running without an owned recovery path.

Ecritum does not claim preemptive thread kill, hard interruption of hostile
in-process code, or universal safe recovery after timeout/cancellation in M2.
Cancellation is cooperative first and backend-enforced only where a language
implementation proves it in Native Image tests.

## Time Source

All deadlines and wait budgets use a monotonic clock. Wall-clock changes do not
extend or shorten execution.

Public C APIs express durations as unsigned nanoseconds. A duration of `0`
means no waiting for wait APIs and immediate deadline expiration when used as an
execution limit. M2 does not define an infinite wait sentinel. `UINT64_MAX` is
reserved and returns `ECRITUM_ERROR_INVALID_ARGUMENT` until a later ADR defines
a sentinel. Callers that need long waits use repeated finite waits.

Swift exposes durations as `Duration` and serializes them losslessly enough for
the C ABI. Invalid or overflowing durations fail before native execution starts.

## Execution Deadline

Every job receives an effective execution deadline from the narrowest applicable
limit:

```text
runtime resource limit
  narrowed by context resource limit
  narrowed by eval options
```

Context and eval limits may narrow but not widen the runtime limit. Missing
limits use the runtime default. Untrusted scripting milestones must configure a
finite default deadline before support is claimed.

When the execution deadline expires:

1. The job records timeout as the terminal cause.
2. The backend adapter requests cancellation or interruption through the
   language-specific mechanism, if one exists.
3. If the adapter can prove the context is safe to reuse, the job becomes
   `ECRITUM_JOB_TIMED_OUT` and `job_result` returns
   `ECRITUM_ERROR_TIMEOUT`.
4. If safe reuse is not proven, the job becomes `ECRITUM_JOB_POISONED`; the
   result status remains `ECRITUM_ERROR_TIMEOUT`, and diagnostics include a
   `context_poisoned` category.

A timeout must never be reported as success. A timeout must not silently destroy
the runtime. Runtime teardown remains explicit.

## Cancellation

`ecritum_job_cancel(job, out_error)` is idempotent. It records a cancellation
request if the job is not terminal.

Cancellation has these race rules:

- If a job already reached a terminal state, the terminal state wins and cancel
  returns `ECRITUM_OK`.
- If cancellation is recorded before normal terminal completion, cancellation is
  the preferred terminal cause unless an execution deadline has already expired.
- If an execution deadline expires before cancellation completes, timeout wins.
- If backend execution fails while cancellation is pending, the more precise
  backend failure may win when it indicates unsafe state. Diagnostics must
  preserve the cancellation request in debug details when raw diagnostics are
  enabled.

If the backend can prove safe cancellation and context reuse, the job becomes
`ECRITUM_JOB_CANCELLED` and `job_result` returns
`ECRITUM_ERROR_CANCELLED`. If not, the job becomes `ECRITUM_JOB_POISONED`,
`job_result` returns `ECRITUM_ERROR_CANCELLED`, and diagnostics include a
`context_poisoned` category.

Cancellation is not a guarantee that a blocked host callback stops immediately.
If a callback is executing when cancellation is requested, Ecritum records
cancel-pending and waits for the callback to return. The callback thread is not
killed by Ecritum in M2.

## Resource Limits

Resource limits are part of Ecritum configuration, not raw GraalVM option
passthroughs. Unknown keys and unknown schema versions fail closed.

M2 resource limits include at least:

- execution timeout
- maximum source/input bytes
- maximum output/string bytes for returned values and diagnostics
- maximum stack/recursion depth when a backend can enforce it
- maximum heap/allocation budget when a backend can enforce it
- callback queue length and callback execution budget

If a backend cannot enforce a limit, the implementation must either:

- reject enabling the corresponding language/support mode, or
- mark the limit as advisory and block untrusted-script support until tests
  prove acceptable recovery behavior.

Permission checks are not disabled during timeout, cancellation, or callback
cleanup. A cancelled job cannot use cleanup paths to bypass filesystem, network,
process, environment, reflection, class loading, native library loading, or raw
host access policy.

## Context And Runtime Recovery

Timeout, cancellation, recursion failure, large allocation failure, callback
failure, and backend internal failure must classify recovery explicitly.

Context recovery states:

- reusable: the context may accept another job after the current terminal job is
  destroyed
- poisoned: the context rejects future work
- closing: destroy is in progress
- destroyed: handle is tombstoned and stale handles fail validation

M2 uses conservative poisoning. Timeout and cancellation poison the context
unless the language backend proves safe reuse in Native Image tests. Script
errors and callback errors may leave the context reusable only when the adapter
proves the backend state is still valid.

M2 does not add a public context state query. A poisoned context rejects future
operations with the original terminal status where precise, otherwise
`ECRITUM_ERROR_INTERNAL`, and includes a stable `context_poisoned` category in
the owned error. Swift maps this to the existing typed status plus recoverability
details; it does not add a public raw handle escape hatch.

If failure makes runtime safety unknown, the runtime becomes poisoned or must be
torn down. New contexts and jobs are rejected. Runtime destroy tombstones
runtime state before backend teardown when reuse is unsafe, following ADR-002.

## Teardown Rules

Normal `ecritum_context_destroy` with an active nonterminal job returns
`ECRITUM_ERROR_BUSY`, leaves the context valid, and does not block
indefinitely.

Normal `ecritum_job_destroy` with an active nonterminal job returns
`ECRITUM_ERROR_BUSY`, leaves the job valid, and does not cancel implicitly.
Callers must call `job_cancel`, wait for a terminal state, then destroy.

`ecritum_runtime_destroy` keeps ADR-002 behavior for live contexts:
`ECRITUM_ERROR_CONTEXTS_ALIVE` and the runtime handle remains valid. If a future
implementation has runtime-owned worker attachments without visible contexts,
destroy returns `ECRITUM_ERROR_BUSY`, leaves the runtime valid, and reports the
operation that is still active.

Runtime teardown with poisoned state may invalidate child handles only when the
runtime cannot be safely reused. In that case, Ecritum tombstones registry state
first, zeros the caller's runtime handle, attempts backend teardown, and returns
`ECRITUM_ERROR_TEARDOWN_FAILED` if backend cleanup fails. Failed teardown never
allows new work on the old handle.

M2 defines no public force-close API. A force-close API would need separate
design for host callback threads, OS resources, cleanup ownership, and process
safety.

## GraalVM And Backend Boundary

Public handles never store a `graal_isolatethread_t`. Native thread attachment
is call-local:

```text
public call enters C wrapper
  attach current native thread to runtime isolate if needed
  call backend adapter
  detach only if this call attached
  return status/error
```

Worker threads used for async jobs follow the same rule. A worker attachment is
not reused by arbitrary OS threads and is not exposed through public handles.

Per-language adapters own cancellation strategy. SCI, LuaJ, GraalJS, GraalPy,
and TruffleRuby may differ. Ecritum must not assume Polyglot
`Context.close(true)` or any other backend-specific primitive exists for all
languages until the corresponding language ADR and Native Image tests prove it.

## Callback Recovery

Host callbacks are trusted host code, but they are still part of the guest job
execution path.

Callback queues have explicit limits. Queue overflow returns
`ECRITUM_ERROR_BUSY` or `ECRITUM_ERROR_CALLBACK` according to the operation that
failed and does not execute guest code out of order.

Callback execution budget is measured with the same monotonic clock model. If a
callback exceeds the job deadline, the job records timeout/cancel-pending.
Ecritum does not kill the callback thread in M2. If the callback returns after
the deadline, the job completes as timeout or poisoned according to backend
recovery. If the callback never returns, the job remains active; context and
runtime destroy return `ECRITUM_ERROR_BUSY` or `ECRITUM_ERROR_CONTEXTS_ALIVE`
without blocking forever.

Callback errors are converted to `ECRITUM_ERROR_CALLBACK` plus redacted
diagnostics. Callback errors do not expose host paths, raw exception class
names, environment values, process commands, or guest source text unless raw
diagnostics are explicitly enabled for a trusted host.

## Swift Mapping

Swift task cancellation calls the native cancel path. Swift cancellation is not
complete until the native job reaches a terminal state or the native layer
reports an unrecoverable/poisoned context.

Swift errors distinguish:

- `.timeout`
- `.cancelled`
- `.busy`
- `.reentrantCall`
- `.callback`
- `.internal`
- `.teardownFailed`

Recoverability is represented in structured details, not by exposing raw native
handles. A poisoned context throws the typed status with a `context_poisoned`
category or equivalent recoverability marker.

Swift `close()` during an active job throws `.busy` and leaves the handle valid.
`deinit` remains best-effort and must not block indefinitely on active guest
execution or a hung callback.

## Verification Requirements

Implementation tasks that claim timeout, cancellation, or untrusted-script
support must add:

- pure tests for deadline selection, narrowing, overflow rejection, and race
  precedence
- C fake-backend tests for cancel idempotence, wait timeout vs execution
  timeout, terminal result ownership, active destroy returning busy, poisoned
  context follow-up errors, and teardown failure tombstoning
- Swift fake-adapter tests for `Task` cancellation, timeout serialization,
  active `close()`, best-effort `deinit`, recoverability details, and no
  `Sendable` claim
- Java/backend tests for catch-all Throwable mapping, interrupt/cancel adapter
  behavior, timeout terminal status, and safe/unsafe reuse classification
- Native Image smoke tests for infinite loop timeout, explicit cancellation,
  recursion/stack blowup, large allocation, callback throw, callback hang,
  callback reentry, and post-timeout/post-cancel reuse behavior
- abuse tests proving denied filesystem, network, process, environment,
  reflection, class loading, native library loading, and raw host access remain
  denied during normal eval, timeout, cancellation, and callback cleanup paths
- deadlock watchdog tests for callback waits, close from callback, sync hop to
  the same Swift executor, concurrent cancel/wait/destroy, and hung callback
  teardown
- ASan/UBSan for C contracts, leak smoke for framework lifecycle, and TSan
  before any handle operation is documented thread-safe
- release gates that include timeout/cancel/teardown smoke and
  `bench-first-eval` once eval exists

Untrusted-user scripting remains blocked until timeout, cancellation,
recursion, allocation, callback failure, permission denial, and teardown
recovery tests pass for the claimed language runtime.

## Rejected Alternatives

Preemptive thread kill was rejected. Killing threads inside a host app process
risks corrupting runtime, host callback, and OS state.

Wrapper-only Swift timeout was rejected. It cancels the Swift task but does not
own native execution or recovery.

Assuming one GraalVM cancellation primitive works for every language was
rejected. Ecritum will include languages with different implementation models.

Force-close in M2 was rejected. It needs separate ownership and safety design
for callbacks, worker threads, and partially cleaned backend state.

## Consequences

The first timeout/cancellation implementation will be conservative. Some
backends may poison contexts after timeout/cancellation until tests prove safe
reuse. That is acceptable because false reuse is more dangerous than requiring a
new context. The ADR gives host registration and eval implementation a concrete
recovery contract without overpromising hard interruption.
