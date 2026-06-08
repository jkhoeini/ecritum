# ClojureNotebook

## Purpose

Evaluate a sequence of Clojure "cells" that share one host-provided dataset,
printing each cell's result. This is the data-exploration use case: the host
owns a stable dataset (daily sales) and runs several independent analytic cells
against it, much like a notebook. All cells run in the same context under the
deny-by-default policy.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.clojure]))` — a deny-by-default,
  single-language runtime.
- `namespace.register(.init("sales")) { call in ... }` returning an
  `EcritumValue.array` — the shared dataset, called from Clojure as `(app/sales)`.
- Repeated `context.eval(EcritumScript(source, language: .clojure, sourceName:))`
  against the same `EcritumContext`, one eval per cell, each with a distinct
  `sourceName` (`cell-1.clj`, `cell-2.clj`, ...).
- Reading back scalars (`.int`) and a structured `.object` result, formatted
  with a small host-side `describe` helper.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-clojure-notebook

# or directly:
cd Examples/ClojureNotebook
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/ClojureNotebook
```

## Expected output

```
Notebook over 7 days of sales
[1] count = 7
[2] total = 891
[3] max-day = 210
[4] above-100 = 4
[5] summary = {days: 7, peak: 210, total: 891}
```

## Security notes

No capabilities are granted. The runtime uses the default
`EcritumPermissionPolicy.defaultDeny` (filesystem, network, process,
environment, clock, random, and log all denied). The dataset crosses the
boundary only as a host-provided value, so no capability is needed.
