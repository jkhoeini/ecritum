# MultiLanguageHost

## Purpose

Show one host application driving a pipeline across all five sandboxed
languages, with a single shared host function carrying state between stages.
The pipeline value is an order subtotal in cents. Each language applies one
transformation stage (handling fee, tax, discount, rounding, reconciliation),
reading the running value from the host and returning the new value. The host
feeds each result into the next stage and prints every language's contribution.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.clojure, .javascript, .lua, .python, .ruby]))`
  — one deny-by-default runtime enabling all five languages.
- `runtime.namespace(.init("app"))` + `namespace.register(.init("current")) { call in ... }`
  — a single host function shared by every language. Clojure invokes it as
  `(app/current)`; the others as `ecritum.app.current()`.
- `context.eval(EcritumScript(source, language:, sourceName:))` called once per
  stage against the same `EcritumContext`, demonstrating that one context
  serves multiple languages.
- `EcritumValue.intValue` to read each stage's integer result.
- Host-owned shared state (a `Sendable` accumulator box) updated between stages,
  showing the host as the source of truth across language boundaries.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-multi-language-host

# or directly:
cd Examples/MultiLanguageHost
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/MultiLanguageHost
```

## Expected output

```
Starting subtotal: $42.00
Clojure: add $5.00 handling fee -> $47.00
JavaScript: apply 8% tax -> $50.76
Lua: $3.00 loyalty discount -> $47.76
Python: round up to nearest dollar -> $48.00
Ruby: add 2 cents rounding reconciliation -> $48.02
Final total: $48.02
```

## Security notes

No capabilities are granted. The runtime uses the default
`EcritumPermissionPolicy.defaultDeny` (filesystem, network, process,
environment, clock, random, and log all denied). State flows only through the
registered host function and host-owned Swift state, so no capability is needed.
