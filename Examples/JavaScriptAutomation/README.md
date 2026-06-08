# JavaScriptAutomation

## Purpose

Run a small JavaScript automation over host-provided structured data using the
classic `filter` / `map` / `reduce` toolkit, and return a structured summary the
host prints. This is the data-automation use case: the host owns an order book,
the guest computes shipped-order revenue, a per-region breakdown, and the top
order. Runs under the deny-by-default policy.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.javascript]))` — a deny-by-default,
  single-language runtime.
- `namespace.register(.init("orders")) { call in ... }` returning an
  `EcritumValue.array` of `.object` records — called from JS as
  `ecritum.app.orders()`.
- `context.eval(EcritumScript(source, language: .javascript, sourceName: "automation.js"))`.
- Returning a JavaScript object that maps back to `EcritumValue.object` (with a
  nested object and string/int fields), then reading it on the host.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-javascript-automation

# or directly:
cd Examples/JavaScriptAutomation
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/JavaScriptAutomation
```

## Expected output

```
Order automation summary:
  regions = {amer: 1, apac: 1, emea: 2}
  revenue = 460
  shippedCount = 4
  topOrder = "A-4"
```

## Security notes

No capabilities are granted. The runtime uses the default
`EcritumPermissionPolicy.defaultDeny` (filesystem, network, process,
environment, clock, random, and log all denied). The order data crosses the
boundary only as a host-provided value, so no capability is needed.
