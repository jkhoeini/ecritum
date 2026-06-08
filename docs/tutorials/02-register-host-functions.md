# 2. Register host functions and call them from a script

Goal: expose host data and behavior to guest scripts, and read results back.

Host functions are the primary, capability-free way to move data across the
boundary. The host owns the data and the trust boundary; the guest calls a
registered function and gets an `EcritumValue` back. No filesystem, network, or
any other capability is needed for this.

## Register a namespace and a function

```swift
import Ecritum
import Foundation

let runtime = try EcritumRuntime(.init(languages: [.javascript, .clojure]))

// A namespace groups host functions. The name must be a valid identifier and
// may not start with a reserved prefix (ecritum, java, javax, sun, graal,
// truffle).
let app = try runtime.namespace(.init("app"))

// Register a function. `EcritumHostFunction` is @Sendable and receives an
// EcritumCall; it returns an EcritumValue.
try app.register(.init("answer")) { call in
    // Validate arity inside the function.
    guard try call.argumentCount() == 0 else {
        throw HostFailure("answer takes no arguments")
    }
    return .int(42)
}

// A function that reads its argument and returns a value.
try app.register(.init("add_one")) { call in
    guard try call.argumentCount() == 1 else {
        throw HostFailure("add_one takes one argument")
    }
    let n = try call.value(at: 0).intValue ?? 0
    return .int(n + 1)
}

let context = try runtime.context()
defer {
    try? context.close()
    try? app.close()
    try? runtime.close()
}

struct HostFailure: Error { let message: String; init(_ m: String) { message = m } }
```

`EcritumCall` gives you `argumentCount()` and `value(at:)`. Arguments and return
values are plain `EcritumValue`s, so you can return structured data with
`.array` and `.object` too.

## Call it from a script — naming convention

The call syntax differs between Clojure and the other four languages:

- **Clojure** uses a slash: `(app/answer)`, `(app/add_one 41)`.
- **JavaScript, Lua, Python, Ruby** address the function under the `ecritum`
  root: `ecritum.app.answer()`, `ecritum.app.add_one(41)`.

A non-Clojure call (JavaScript):

```swift
let js = try await context.eval(EcritumScript(
    "ecritum.app.add_one(ecritum.app.answer() - 1)",
    language: .javascript,
    sourceName: "host.js"
))
print(js.intValue ?? -1) // 42
```

The same shape in Clojure:

```swift
let clj = try await context.eval(EcritumScript(
    "(app/add_one (- (app/answer) 1))",
    language: .clojure,
    sourceName: "host.clj"
))
print(clj.intValue ?? -1) // 42
```

## Returning and reading structured values

A host function can return a nested `.object`/`.array`, and the guest can return
structured data the host reads back:

```swift
try app.register(.init("invoice")) { _ in
    .object([
        "id": .string("INV-1"),
        "lines": .array([
            .object(["label": .string("Seat"), "qty": .int(3), "unit": .double(45.0)]),
        ]),
    ])
}
```

In the guest, address fields normally (e.g. Ruby `ecritum.app.invoice` returns a
map you index, or Clojure `(app/invoice)` returns a map). On the host, read the
returned `EcritumValue` with `.objectValue`, `.arrayValue`, `.stringValue`, etc.

## Host-owned shared state

Because `EcritumHostFunction` is `@Sendable`, keep mutable host state in a small
thread-safe box and read/update it around evals. This makes the host the single
source of truth across language boundaries.

```swift
final class Counter: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Int64 = 0
    var current: Int64 { lock.lock(); defer { lock.unlock() }; return value }
    func set(_ v: Int64) { lock.lock(); value = v; lock.unlock() }
}
```

## Runnable example

[`Examples/MultiLanguageHost`](../../Examples/MultiLanguageHost) registers a
single shared host function `current` and runs a five-stage pipeline, one stage
per language, threading host-owned state between stages. It shows both the
Clojure `(app/current)` form and the `ecritum.app.current()` form for the other
four.

```sh
mise exec -- just example-multi-language-host
```

Verified output:

```
Starting subtotal: $42.00
Clojure: add $5.00 handling fee -> $47.00
JavaScript: apply 8% tax -> $50.76
Lua: $3.00 loyalty discount -> $47.76
Python: round up to nearest dollar -> $48.00
Ruby: add 2 cents rounding reconciliation -> $48.02
Final total: $48.02
```

See also the single-language host-function examples:
[RubyTemplateRenderer](../../Examples/RubyTemplateRenderer),
[ClojureNotebook](../../Examples/ClojureNotebook),
[JavaScriptAutomation](../../Examples/JavaScriptAutomation),
[LuaRulesEngine](../../Examples/LuaRulesEngine),
[PythonTextProcessing](../../Examples/PythonTextProcessing).

Next: [scripts in each language](03-scripts-in-each-language.md).
