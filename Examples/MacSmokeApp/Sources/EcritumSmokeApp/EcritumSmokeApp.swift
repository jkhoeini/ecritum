import Ecritum
import Foundation

@main
struct EcritumSmokeApp {
    static func main() async {
        do {
            guard Ecritum.runtimeArtifactAvailable else {
                throw SmokeFailure("runtime artifact is not available")
            }

            let version = try Ecritum.version
            let runtime = try EcritumRuntime(.init(languages: [.clojure, .javascript]))
            let namespace = try runtime.namespace(.init("app"))
            try namespace.register(.init("answer")) { call in
                guard try call.argumentCount() == 0 else {
                    throw SmokeFailure("answer callback expected no arguments")
                }
                return .int(42)
            }
            let context = try runtime.context()
            defer {
                try? context.close()
                try? namespace.close()
                try? runtime.close()
            }

            let clojure = try await context.eval(EcritumScript(
                "(+ 40 (- (app/answer) 40))",
                language: .clojure,
                sourceName: "packaged-smoke.clj"
            ))
            try expectInt(clojure, equals: 42, label: "clojure")

            let javascript = try await context.eval(EcritumScript(
                "40 + (ecritum.app.answer() - 40)",
                language: .javascript,
                sourceName: "packaged-smoke.js"
            ))
            try expectInt(javascript, equals: 42, label: "javascript")

            print("EcritumSmokeApp version=\(version) clojure=42 javascript=42")
        } catch {
            fputs("EcritumSmokeApp failed: \(error)\n", stderr)
            exit(1)
        }
    }

    private static func expectInt(_ value: EcritumValue, equals expected: Int64, label: String) throws {
        guard value == .int(expected) else {
            throw SmokeFailure("\(label) expected \(expected), got \(value)")
        }
    }
}

private struct SmokeFailure: Error, CustomStringConvertible {
    var description: String

    init(_ description: String) {
        self.description = description
    }
}
