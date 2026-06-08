import Ecritum
import Foundation

// MultiLanguageHost
// -----------------
// One host application registers a shared host function and runs a five-stage
// pipeline, one stage per language. The pipeline value is an order subtotal in
// cents. Each stage reads the running value from the host (via the shared host
// function `current`), applies one transformation, and returns the new value.
// The host feeds each result back into the next stage and prints every
// language's contribution. Everything runs under the deny-by-default policy.

struct PipelineFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// A pipeline stage: which language runs it, its source, and a label.
struct Stage {
    let language: EcritumLanguage
    let label: String
    let sourceName: String
    let source: String
}

let startingCents: Int64 = 4200

// Each stage reads `current` (the running subtotal) and returns a new Int.
// Clojure calls host functions as `(app/current)`; the other four call them as
// `ecritum.app.current()`.
let stages: [Stage] = [
    Stage(
        language: .clojure,
        label: "Clojure: add $5.00 handling fee",
        sourceName: "stage.clj",
        source: "(+ (app/current) 500)"
    ),
    Stage(
        language: .javascript,
        label: "JavaScript: apply 8% tax",
        sourceName: "stage.js",
        source: "Math.round(ecritum.app.current() * 1.08)"
    ),
    Stage(
        language: .lua,
        label: "Lua: $3.00 loyalty discount",
        sourceName: "stage.lua",
        source: "return ecritum.app.current() - 300"
    ),
    Stage(
        language: .python,
        label: "Python: round up to nearest dollar",
        sourceName: "stage.py",
        source: "((ecritum.app.current() + 99) // 100) * 100"
    ),
    Stage(
        language: .ruby,
        label: "Ruby: add 2 cents rounding reconciliation",
        sourceName: "stage.rb",
        source: "ecritum.app.current() + 2"
    ),
]

// Shared, host-owned pipeline state. The host function reads it; the host
// updates it after each stage. `EcritumHostFunction` is @Sendable, so the
// running value lives in a small reference box.
final class Accumulator: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Int64
    init(_ value: Int64) { self.value = value }
    var current: Int64 {
        lock.lock(); defer { lock.unlock() }
        return value
    }
    func set(_ newValue: Int64) {
        lock.lock(); defer { lock.unlock() }
        value = newValue
    }
}

func format(cents: Int64) -> String {
    let dollars = Double(cents) / 100.0
    return String(format: "$%.2f", dollars)
}

func run() async throws {
    guard Ecritum.runtimeArtifactAvailable else {
        throw PipelineFailure("runtime artifact is not available")
    }

    let runtime = try EcritumRuntime(.init(languages: [.clojure, .javascript, .lua, .python, .ruby]))

    let accumulator = Accumulator(startingCents)

    // One shared host function used by every language stage.
    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("current")) { call in
        guard try call.argumentCount() == 0 else {
            throw PipelineFailure("current host function takes no arguments")
        }
        return .int(accumulator.current)
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    print("Starting subtotal: \(format(cents: startingCents))")
    for stage in stages {
        let result = try await context.eval(EcritumScript(
            stage.source,
            language: stage.language,
            sourceName: stage.sourceName
        ))
        guard let newValue = result.intValue else {
            throw PipelineFailure("\(stage.label) returned non-integer: \(result)")
        }
        accumulator.set(newValue)
        print("\(stage.label) -> \(format(cents: newValue))")
    }
    print("Final total: \(format(cents: accumulator.current))")
}

do {
    try await run()
} catch {
    fputs("MultiLanguageHost failed: \(error)\n", stderr)
    exit(1)
}
