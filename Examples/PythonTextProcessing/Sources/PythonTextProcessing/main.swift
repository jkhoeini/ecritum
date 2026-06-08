import Ecritum
import Foundation

// PythonTextProcessing
// --------------------
// The host hands a block of text to a sandboxed Python script. Python computes
// counts and a transformed headline using only its in-language standard library
// (no host facades, no filesystem), and returns a structured summary the host
// prints. This is the text-analytics use case. Runs under the deny-by-default
// policy with NO capabilities granted: the document is passed as host data, not
// read from disk, so the example stays entirely capability-free.

struct TextFailure: Error, CustomStringConvertible {
    var description: String
    init(_ description: String) { self.description = description }
}

// Host-owned document. In a real app this might come from a request body or a
// database column; here it is a literal so the example is deterministic.
let document = """
release notes for ecritum
ecritum sandboxes five languages safely
each language runs under a deny by default policy
host data flows through typed values
"""

// The Python program: count lines, words and characters, find the single most
// common word, and build an upper-cased headline from the first line. It uses
// only Python builtins (str methods, dict, sorted) — no `import`, and no host
// facade such as ecritum.fs / ecritum.http / ecritum.time — and returns a dict.
// The most-common word is resolved deterministically: highest count first, then
// lexicographically, so ties never produce nondeterministic output.
let program = #"""
text = ecritum.app.document()
lines = text.splitlines()
words = text.split()
counts = {}
for word in words:
    counts[word] = counts.get(word, 0) + 1
ranked = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
top_word, top_count = ranked[0]
{
    "lines": len(lines),
    "words": len(words),
    "characters": len(text),
    "topWord": top_word,
    "topWordCount": top_count,
    "headline": lines[0].upper(),
}
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
        throw TextFailure("runtime artifact is not available")
    }

    let runtime = try EcritumRuntime(.init(languages: [.python]))

    let namespace = try runtime.namespace(.init("app"))
    try namespace.register(.init("document")) { call in
        guard try call.argumentCount() == 0 else {
            throw TextFailure("document host function takes no arguments")
        }
        return .string(document)
    }

    let context = try runtime.context()
    defer {
        try? context.close()
        try? namespace.close()
        try? runtime.close()
    }

    let result = try await context.eval(EcritumScript(
        program,
        language: .python,
        sourceName: "analyze.py"
    ))

    guard case let .object(summary) = result else {
        throw TextFailure("expected a summary object, got \(result)")
    }
    print("Text analysis:")
    for key in summary.keys.sorted() {
        print("  \(key) = \(describe(summary[key]!))")
    }
}

do {
    try await run()
} catch {
    fputs("PythonTextProcessing failed: \(error)\n", stderr)
    exit(1)
}
