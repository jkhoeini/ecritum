# Ecritum Agent Instructions

## Project Shape

Ecritum is a macOS-first embeddable polyglot scripting runtime for Swift desktop apps. The public integration surface is SwiftPM plus a stable C ABI, so other language wrappers can be added later without exposing a C++ or JVM-specific API.

The planned core is a GraalVM Native Image shared library (`.dylib` or `XCFramework`) that embeds a curated set of runtimes and exposes `ecritum_*` C functions. The app developer should not need to install GraalVM, a JDK, Python, Ruby, Node, or Clojure separately to use the packaged runtime.

## Hard Boundaries

- Use `jj` for version control. Do not use `git` commands.
- Use `mise` for project tools and `just` for project tasks. Prefer `mise exec -- just <task>` if tool versions matter.
- Keep the public ABI C-compatible: opaque handles, explicit ownership, explicit error objects, no exceptions crossing FFI.
- Treat Swift as the primary developer experience, but keep the core host API language-neutral.
- Do not claim arbitrary JVM/JAR support until a separate Espresso/full-JVM design is implemented, tested, and licensed.
- Do not bundle a full JDK unless a design document explicitly accepts the size, licensing, and distribution cost.
- Keep dynamic-library distribution as the default plan: single binary where practical, otherwise a small set of bundled dylibs.
- Do not rely on SwiftPM running GraalVM builds for consumers. Consumer packages must use prebuilt binary artifacts.

## Runtime Plan

- First-class host API: register host functions, callbacks, values, and capability objects from C/Swift.
- First-class Clojure scripting: use SCI with Babashka-compatible namespaces and behavior.
- JavaScript support: target GraalJS.
- Python support: target GraalPy, with size and resource packaging gates.
- Ruby support: target TruffleRuby, with size, licensing, and resource packaging gates.
- Lua support: start with LuaJ or another pure-Java implementation that works in Native Image; do not assume an official Truffle Lua runtime exists.
- JVM-language support: Clojure via SCI first; arbitrary Kotlin/Clojure/JAR execution is not an MVP feature.

## Engineering Rules

- Keep host capabilities explicit. Scripts should only access APIs the host registered.
- Design cancellation, timeouts, resource limits, and error reporting before exposing untrusted-user scripting.
- Track binary size, cold start time, and memory overhead as product requirements.
- Maintain a license inventory for GraalVM CE, Native Image/SubstrateVM, Truffle, GraalJS, GraalPy, TruffleRuby, SCI, and transitive dependencies.
- Prefer small examples that prove distribution behavior: a C host, a SwiftPM host, and a packaged macOS app loading the runtime.
- Document every public C symbol before stabilizing it.
- Expose useful JDK capabilities through a curated Ecritum standard library. Do not enable unrestricted Java class lookup, reflection, class loading, native library loading, process execution, filesystem, network, or environment access by default.

## Task Commands

- `just setup`: install project tools with mise.
- `just doctor`: print tool versions.
- `just test`: run currently available checks.
- `just native`: build the Native Image shared library once source scaffolding exists.
- `just xcframework`: assemble the SwiftPM binary artifact once native artifacts exist.
- `just license-report`: generate third-party notices once dependencies exist.

Use `mise trust` once before running project commands on a fresh checkout.

## Team Operating Model

- Treat the assistant as the engineering manager for this project.
- `TEAM.md` defines the durable team personas. Read it before planning or
  assigning non-trivial work.
- `PROJECT.org` is the project-management source of truth. Read it before
  implementation, update task status as work progresses, and record verification
  evidence before marking tasks done.
- `DEBT.md` is the technical-debt ledger. Any accepted shortcut must include
  owner, date, impact, resolve-by phase, exit condition, removal task, and
  verification.
- For each non-trivial task, choose the smallest useful set of personas from
  `TEAM.md`, create fresh sub-agents for them, and assign each sub-agent a
  specific `PROJECT.org` task, responsibility, write scope, review duty, and
  verification plan.
- Sub-agents must read their assigned task from `PROJECT.org` for context and
  move it to REVIEW only after implementation, verification, and review evidence
  exist. Only the engineering manager marks tasks DONE.
- Keep parallel sub-agent write scopes disjoint. No agent may revert or overwrite
  another agent's edits unless the engineering manager explicitly assigns that
  cleanup.

## Planning and Review Workflow

- Research first. Before implementation, read the relevant docs, source, tests,
  build scripts, and upstream references needed to understand the full picture.
- Every non-trivial plan must discuss architecture impact, code quality impact,
  testability, technical debt, security/capability impact, distribution impact,
  and verification.
- Keep configuration parsing, permission decisions, value conversion, capability
  lookup, error classification, and lifecycle state transitions as pure value
  transformations with table-driven tests where practical.
- Keep side effects behind named adapters: native runtime, host callback,
  standard-library capability, clock/random, IO/network/process/env, and Swift
  executor bridge.
- Use Claude CLI as an independent reviewer for every meaningful plan and
  implementation. Run at least one review before implementation and one after
  implementation. Iterate on the feedback and run one second review after fixes;
  after that, continue only for blocking findings or record follow-up tasks.
- In this repo, Claude CLI works without an auth permission check. Skip any
  Claude auth-check step and call `claude -p` directly.
- If Claude CLI is unavailable, record the blocker in `PROJECT.org` and use
  persona sub-agent review as the fallback. Do not silently skip review.
- If a decision affects public ABI, architecture, security, distribution,
  runtime support, licensing, or long-term maintenance, write an ADR under
  `docs/adr/` before implementation.
- Each plan must include verification commands. Each completed task must record
  the commands run and their result in `PROJECT.org`.
- Each language runtime must pass the shared conformance suite before support is
  claimed.
- PLAN.org is the technical seed plan. PROJECT.org controls execution. Migrate
  implementation-critical PLAN.org decisions into ADRs or PROJECT.org tasks
  before coding against them.
- Non-trivial means any task that changes more than one file, touches public API
  or ABI, changes build/test/release/security behavior, adds a dependency,
  changes runtime support, or creates a user-visible promise.

## Naming

Ecritum is a coined name inspired by French `ecrit`/`ecriture` and Latin `scriptum`: a written thing, a script, or an inscription. Use the plain ASCII spelling `Ecritum` in package names, symbols, and filenames.
