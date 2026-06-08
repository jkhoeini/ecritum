# 3. Write and evaluate scripts in each language

Goal: run a short script in each of the five supported languages.

The default artifact ships all five languages. Select a language with the
`EcritumLanguage` value and the matching `EcritumScript(..., language:)`. The
value an expression yields maps back to an `EcritumValue`.

Enable only the languages you use:

```swift
let runtime = try EcritumRuntime(.init(
    languages: [.clojure, .javascript, .lua, .python, .ruby]
))
let context = try runtime.context()
defer { try? context.close(); try? runtime.close() }
```

Each snippet below returns an integer for clarity; structured returns map to
`.array`/`.object`.

## Clojure (`.clojure`)

SCI, with Babashka-compatible namespaces.

```swift
let v = try await context.eval(EcritumScript(
    "(reduce + [1 2 3 4])",
    language: .clojure,
    sourceName: "demo.clj"
))
// v == .int(10)
```

See [`Examples/ClojureNotebook`](../../Examples/ClojureNotebook) — a notebook of
Clojure cells over one shared dataset.

## JavaScript (`.javascript`)

GraalJS. The trailing expression is the result.

```swift
let v = try await context.eval(EcritumScript(
    "[1, 2, 3, 4].reduce((a, b) => a + b, 0)",
    language: .javascript,
    sourceName: "demo.js"
))
// v == .int(10)
```

See [`Examples/JavaScriptAutomation`](../../Examples/JavaScriptAutomation) —
filter/map/reduce over host-provided records.

## Lua (`.lua`)

LuaJ. Use `return` to yield a value.

```swift
let v = try await context.eval(EcritumScript(
    "return 1 + 2 + 3 + 4",
    language: .lua,
    sourceName: "demo.lua"
))
// v == .int(10)
```

See [`Examples/LuaRulesEngine`](../../Examples/LuaRulesEngine) — one Lua ruleset
over several host-provided fact sets.

## Python (`.python`)

GraalPy. **Standard library only** — there is no `pip` or third-party package
install, and the sandbox seals `__import__`, so `import` statements are
rejected. Stick to builtins.

```swift
let v = try await context.eval(EcritumScript(
    "sum([1, 2, 3, 4])",
    language: .python,
    sourceName: "demo.py"
))
// v == .int(10)
```

See [`Examples/PythonTextProcessing`](../../Examples/PythonTextProcessing) —
text analytics using only Python builtins.

## Ruby (`.ruby`)

TruffleRuby in pure-Ruby sandboxed mode (LLVM/Sulong backend excluded).
**Runtime and standard library only** — no RubyGems, Bundler, native gems, or
C/native extensions. The last expression is the result.

```swift
let v = try await context.eval(EcritumScript(
    "[1, 2, 3, 4].sum",
    language: .ruby,
    sourceName: "demo.rb"
))
// v == .int(10)
```

See [`Examples/RubyTemplateRenderer`](../../Examples/RubyTemplateRenderer) —
rendering a report from host-owned data in sandboxed Ruby.

## Run all five at once

[`Examples/MultiLanguageHost`](../../Examples/MultiLanguageHost) evaluates one
stage per language against a single context:

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

Or run each per-language example directly:

```sh
mise exec -- just example-clojure-notebook
mise exec -- just example-javascript-automation
mise exec -- just example-lua-rules-engine
mise exec -- just example-python-text-processing
mise exec -- just example-ruby-template-renderer
```

For the Python/Ruby package limits in user-facing terms, see
[Tutorial 6](06-interpret-errors-and-denials.md).

Next: [enable a narrow filesystem capability](04-narrow-filesystem-capability.md).
