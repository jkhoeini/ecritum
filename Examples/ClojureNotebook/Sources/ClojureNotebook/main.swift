import Ecritum
import Foundation

// ClojureNotebook
// ---------------
// A minimal notebook: the host provides one shared dataset (daily sales) via a
// host function, then evaluates a sequence of Clojure cells against the same
// context. Each cell reads the dataset with `(app/sales)`, computes a result,
// and the host prints it. This is the data-exploration use case: stable
// host-owned data, several analytic cells. Runs under the deny-by-default policy.

struct NotebookFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// Shared dataset, owned by the host. Daily sales in whole units.
let sales: [Int64] = [120, 95, 143, 88, 210, 175, 60]

struct Cell {
    let title: String
    let source: String
}

// Each cell is an independent Clojure expression sharing `(app/sales)`.
let cells: [Cell] = [
    Cell(title: "count", source: "(count (app/sales))"),
    Cell(title: "total", source: "(reduce + (app/sales))"),
    Cell(title: "max-day", source: "(apply max (app/sales))"),
    Cell(title: "above-100", source: "(count (filter #(> % 100) (app/sales)))"),
    Cell(title: "summary", source: "(let [s (app/sales)] {:days (count s) :total (reduce + s) :peak (apply max s)})"),
]

// Render an EcritumValue for human-readable notebook output. This is just
// host-side presentation; it does not hide any of the runtime API.
func describe(_ value: EcritumValue) -> String {
    switch value {
    case .null: return "nil"
    case let .bool(b): return String(b)
    case let .int(i): return String(i)
    case let .double(d): return String(d)
    case let .string(s): return "\"\(s)\""
    case let .data(d): return "<\(d.count) bytes>"
    case let .array(items): return "[" + items.map(describe).joined(separator: " ") + "]"
    case let .object(fields):
        let body = fields.sorted { $0.key < $1.key }
            .map { "\($0.key): \(describe($0.value))" }
            .joined(separator: ", ")
        return "{" + body + "}"
    }
}

func run() async throws {
    guard Ecritum.runtimeArtifactAvailable else {
        throw NotebookFailure("runtime artifact is not available")
    }

    let runtime = try EcritumRuntime(.init(languages: [.clojure]))

    // The shared dataset host function: `(app/sales)` returns the array.
    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("sales")) { call in
        guard try call.argumentCount() == 0 else {
            throw NotebookFailure("sales host function takes no arguments")
        }
        return .array(sales.map { .int($0) })
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    print("Notebook over \(sales.count) days of sales")
    for (index, cell) in cells.enumerated() {
        let result = try await context.eval(EcritumScript(
            cell.source,
            language: .clojure,
            sourceName: "cell-\(index + 1).clj"
        ))
        print("[\(index + 1)] \(cell.title) = \(describe(result))")
    }
}

do {
    try await run()
} catch {
    fputs("ClojureNotebook failed: \(error)\n", stderr)
    exit(1)
}
