import Ecritum
import Foundation

// LuaRulesEngine
// --------------
// The host evaluates one Lua ruleset against several host-provided fact sets
// (loan applications) and prints the decision for each. The host owns the facts
// and the rule text; Lua evaluates the rules and returns a structured decision
// (approved + reasons). This is the policy/rules-engine use case. Runs under the
// deny-by-default policy.

struct RulesFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// Host-owned applications to score.
let applications: [(name: String, facts: EcritumValue)] = [
    ("Dana", .object(["age": .int(34), "income": .int(72000), "amount": .int(15000), "verified": .bool(true)])),
    ("Lee", .object(["age": .int(19), "income": .int(28000), "amount": .int(9000), "verified": .bool(true)])),
    ("Sam", .object(["age": .int(41), "income": .int(50000), "amount": .int(40000), "verified": .bool(false)])),
]

// The Lua ruleset: read the current facts, apply each rule, collect failures,
// and return a decision table. An empty `reasons` list means approved.
let ruleset = #"""
local f = ecritum.app.facts()
local reasons = {}
if f.age < 21 then
  table.insert(reasons, "applicant must be at least 21")
end
if f.amount > f.income / 2 then
  table.insert(reasons, "loan exceeds half of annual income")
end
if not f.verified then
  table.insert(reasons, "identity not verified")
end
return { approved = (#reasons == 0), reasons = reasons }
"""#

// Host-owned slot for the application currently under evaluation.
final class FactsSlot: @unchecked Sendable {
    private let lock = NSLock()
    private var facts: EcritumValue = .null
    var current: EcritumValue {
        lock.lock(); defer { lock.unlock() }
        return facts
    }
    func set(_ value: EcritumValue) {
        lock.lock(); defer { lock.unlock() }
        facts = value
    }
}

func run() async throws {
    guard Ecritum.runtimeArtifactAvailable else {
        throw RulesFailure("runtime artifact is not available")
    }

    let runtime = try EcritumRuntime(.init(languages: [.lua]))

    let slot = FactsSlot()
    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("facts")) { call in
        guard try call.argumentCount() == 0 else {
            throw RulesFailure("facts host function takes no arguments")
        }
        return slot.current
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    print("Loan rules engine:")
    for application in applications {
        slot.set(application.facts)
        let decision = try await context.eval(EcritumScript(
            ruleset,
            language: .lua,
            sourceName: "ruleset.lua"
        ))
        guard case let .object(fields) = decision,
              case let .bool(approved)? = fields["approved"] else {
            throw RulesFailure("expected a decision table, got \(decision)")
        }
        let reasons = fields["reasons"]?.arrayValue?.compactMap { $0.stringValue } ?? []
        if approved {
            print("  \(application.name): APPROVED")
        } else {
            print("  \(application.name): DENIED (\(reasons.joined(separator: "; ")))")
        }
    }
}

do {
    try await run()
} catch {
    fputs("LuaRulesEngine failed: \(error)\n", stderr)
    exit(1)
}
