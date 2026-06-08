# LuaRulesEngine

## Purpose

Evaluate one Lua ruleset against several host-provided fact sets and return a
structured decision for each. This is the policy/rules-engine use case: the host
owns both the facts (loan applications) and the rule text, while Lua evaluates
the rules and returns an `{ approved, reasons }` decision table. The host prints
the verdict for each application. Runs under the deny-by-default policy.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.lua]))` — a deny-by-default,
  single-language runtime.
- `namespace.register(.init("facts")) { call in ... }` returning the current
  application as an `EcritumValue.object` — called from Lua as
  `ecritum.app.facts()`.
- Re-evaluating the same `EcritumScript` against the same `EcritumContext` once
  per application, with host-owned state (a `Sendable` slot) swapped between
  evals.
- Reading back a Lua table as `EcritumValue.object`, including a nested array
  (`reasons`) mapped to `EcritumValue.array`, via `.boolValue` / `.arrayValue` /
  `.stringValue`.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-lua-rules-engine

# or directly:
cd Examples/LuaRulesEngine
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/LuaRulesEngine
```

## Expected output

```
Loan rules engine:
  Dana: APPROVED
  Lee: DENIED (applicant must be at least 21)
  Sam: DENIED (loan exceeds half of annual income; identity not verified)
```

## Security notes

No capabilities are granted. The runtime uses the default
`EcritumPermissionPolicy.defaultDeny` (filesystem, network, process,
environment, clock, random, and log all denied). Facts cross the boundary only
as host-provided values, so no capability is needed.
