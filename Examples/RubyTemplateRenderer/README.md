# RubyTemplateRenderer

## Purpose

Render a text report in sandboxed Ruby from host-owned data. The Swift host
holds an invoice record and exposes it through a host function; the Ruby script
pulls the record, computes the line-item total, and returns a formatted
plain-text invoice. This is the report-generation use case: the host owns the
data and trust boundary, the guest owns the presentation template.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.ruby]))` — a deny-by-default runtime scoped
  to a single language.
- `runtime.namespace(.init("app"))` and `namespace.register(.init("invoice")) { call in ... }`
  — registering a host function that returns a structured `EcritumValue` (a
  nested `.object` / `.array`). The guest calls it as `ecritum.app.invoice`.
- `EcritumCall.argumentCount()` — validating arity inside the host function.
- `context.eval(EcritumScript(..., language: .ruby, sourceName: "invoice.rb"))`
  — async evaluation returning an `EcritumValue`.
- `EcritumValue.stringValue` — reading the rendered string back on the host.
- Lifecycle cleanup via `context.close()` / `namespace.close()` / `runtime.close()`.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-ruby-template-renderer

# or directly:
cd Examples/RubyTemplateRenderer
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/RubyTemplateRenderer
```

## Expected output

```
INVOICE INV-2026-0007 for Acme Robotics (USD)
  Design retainer      1 x  2400.00 =    2400.00
  Sandbox seats       12 x    45.00 =     540.00
  Priority support     3 x   150.00 =     450.00
  TOTAL                                    3390.00
```

## Security notes

No capabilities are granted. The runtime is created with the default
`EcritumPermissionPolicy.defaultDeny`: filesystem, network, process,
environment, clock, random, and log are all denied. All data crosses the
boundary as host-provided values, so the example needs no filesystem or any
other capability.
