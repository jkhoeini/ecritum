import Ecritum
import Foundation

// JavaScriptAutomation
// --------------------
// The host hands an array of order records to a sandboxed JavaScript script.
// The script runs a small ETL automation over the data with map / filter /
// reduce, and returns a structured summary object that the host prints. This is
// the "automation over structured data" use case. Runs under the
// deny-by-default policy with no capabilities granted.

struct AutomationFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// Host-owned order book. Each order is an object the host controls.
let orders: EcritumValue = .array([
    .object(["id": .string("A-1"), "region": .string("emea"), "status": .string("shipped"), "amount": .int(120)]),
    .object(["id": .string("A-2"), "region": .string("amer"), "status": .string("pending"), "amount": .int(80)]),
    .object(["id": .string("A-3"), "region": .string("emea"), "status": .string("shipped"), "amount": .int(45)]),
    .object(["id": .string("A-4"), "region": .string("apac"), "status": .string("shipped"), "amount": .int(200)]),
    .object(["id": .string("A-5"), "region": .string("amer"), "status": .string("shipped"), "amount": .int(95)]),
    .object(["id": .string("A-6"), "region": .string("emea"), "status": .string("cancelled"), "amount": .int(60)]),
])

// The JavaScript automation: filter to shipped orders, sum revenue, and build a
// per-region count. Returns a plain object that maps back to an EcritumValue.
let automation = #"""
const shipped = ecritum.app.orders().filter(o => o.status === "shipped");
const revenue = shipped.reduce((sum, o) => sum + o.amount, 0);
const byRegion = shipped.reduce((acc, o) => {
  acc[o.region] = (acc[o.region] || 0) + 1;
  return acc;
}, {});
({
  shippedCount: shipped.length,
  revenue: revenue,
  regions: byRegion,
  topOrder: shipped.reduce((best, o) => o.amount > best.amount ? o : best).id
});
"""#

func describe(_ value: EcritumValue) -> String {
    switch value {
    case .null: return "null"
    case let .bool(b): return String(b)
    case let .int(i): return String(i)
    case let .double(d): return d == d.rounded() ? String(Int64(d)) : String(d)
    case let .string(s): return "\"\(s)\""
    case let .data(d): return "<\(d.count) bytes>"
    case let .array(items): return "[" + items.map(describe).joined(separator: ", ") + "]"
    case let .object(fields):
        let body = fields.sorted { $0.key < $1.key }
            .map { "\($0.key): \(describe($0.value))" }
            .joined(separator: ", ")
        return "{" + body + "}"
    }
}

func run() async throws {
    guard Ecritum.runtimeArtifactAvailable else {
        throw AutomationFailure("runtime artifact is not available")
    }

    let runtime = try EcritumRuntime(.init(languages: [.javascript]))

    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("orders")) { call in
        guard try call.argumentCount() == 0 else {
            throw AutomationFailure("orders host function takes no arguments")
        }
        return orders
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    let result = try await context.eval(EcritumScript(
        automation,
        language: .javascript,
        sourceName: "automation.js"
    ))

    guard case let .object(summary) = result else {
        throw AutomationFailure("expected a summary object, got \(result)")
    }
    print("Order automation summary:")
    for key in summary.keys.sorted() {
        print("  \(key) = \(describe(summary[key]!))")
    }
}

do {
    try await run()
} catch {
    fputs("JavaScriptAutomation failed: \(error)\n", stderr)
    exit(1)
}
