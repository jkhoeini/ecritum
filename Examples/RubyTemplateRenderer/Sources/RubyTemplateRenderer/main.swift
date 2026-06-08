import Ecritum
import Foundation

// RubyTemplateRenderer
// --------------------
// A host application hands a structured invoice record to a sandboxed Ruby
// script, which renders it into a formatted plain-text invoice and returns the
// rendered string to the host. The host owns the data; the guest owns the
// presentation logic. No filesystem, network, process, or native capability is
// granted: the runtime uses the deny-by-default policy.

struct RendererFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// The record the host wants rendered. In a real app this would come from a
// database or API; here it is a literal so the example is deterministic.
let invoice: EcritumValue = .object([
    "number": .string("INV-2026-0007"),
    "customer": .string("Acme Robotics"),
    "currency": .string("USD"),
    "lineItems": .array([
        .object(["description": .string("Design retainer"), "quantity": .int(1), "unitPrice": .double(2400.0)]),
        .object(["description": .string("Sandbox seats"), "quantity": .int(12), "unitPrice": .double(45.0)]),
        .object(["description": .string("Priority support"), "quantity": .int(3), "unitPrice": .double(150.0)]),
    ]),
])

// The Ruby template. It pulls the record from the host via `ecritum.app.invoice`,
// computes the total, and formats a fixed-width report. Returning a single
// String keeps the host-visible output trivial to verify.
let template = #"""
record = ecritum.app.invoice
lines = record["lineItems"]
total = lines.reduce(0.0) { |sum, item| sum + item["quantity"] * item["unitPrice"] }
rows = lines.map do |item|
  amount = item["quantity"] * item["unitPrice"]
  format("  %-18s %3d x %8.2f = %10.2f", item["description"], item["quantity"], item["unitPrice"], amount)
end
header = format("INVOICE %s for %s (%s)", record["number"], record["customer"], record["currency"])
[header, *rows, format("  %-37s %10.2f", "TOTAL", total)].join("\n")
"""#

func run() async throws {
    guard Ecritum.runtimeArtifactAvailable else {
        throw RendererFailure("runtime artifact is not available")
    }

    // Deny-by-default runtime restricted to Ruby only.
    let runtime = try EcritumRuntime(.init(languages: [.ruby]))

    // Register a host function that returns the invoice record. The guest calls
    // it as `ecritum.app.invoice`.
    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("invoice")) { call in
        guard try call.argumentCount() == 0 else {
            throw RendererFailure("invoice host function takes no arguments")
        }
        return invoice
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    let result = try await context.eval(EcritumScript(
        template,
        language: .ruby,
        sourceName: "invoice.rb"
    ))

    guard let rendered = result.stringValue else {
        throw RendererFailure("expected a rendered String, got \(result)")
    }
    print(rendered)
}

do {
    try await run()
} catch {
    fputs("RubyTemplateRenderer failed: \(error)\n", stderr)
    exit(1)
}
