import CEcritum
import Foundation
import XCTest
@testable import Ecritum

final class EcritumEvalTests: XCTestCase {
    func testScriptDescriptorStoresSourceLanguageSourceNameAndDefaultOptions() {
        let script = EcritumScript("(inc 41)", language: .clojure, sourceName: "smoke.clj")

        XCTAssertEqual(script.source, "(inc 41)")
        XCTAssertEqual(script.language, .clojure)
        XCTAssertEqual(script.sourceName, "smoke.clj")
        XCTAssertEqual(script.options, .default)
    }

    func testContextEvalUsesFiniteJobResultValueAndDestroySequence() async throws {
        let adapter = FakeEvalABI()
        adapter.waitStates = [.succeeded]
        adapter.copiedValues[500] = .object(["answer": .int(42)])
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()

        let value = try await context.eval(EcritumScript("fixture:int:42", language: .clojure, sourceName: "smoke.clj"))

        XCTAssertEqual(value, .object(["answer": .int(42)]))
        XCTAssertEqual(adapter.events, [
            "runtimeCreate",
            "contextCreate",
            "evalStart:clojure:smoke.clj",
            "jobWait:200",
            "jobResult:200",
            "copyValue:500",
            "valueDestroy:500",
            "jobDestroy:200",
        ])
        XCTAssertEqual(adapter.waitTimeouts.count, 1)
        XCTAssertGreaterThan(adapter.waitTimeouts[0], 0)
        XCTAssertNotEqual(adapter.waitTimeouts[0], UInt64.max)
    }

    func testContextEvalDestroysJobWhenResultThrows() async throws {
        let adapter = FakeEvalABI()
        adapter.waitStates = [.failed]
        adapter.resultError = .script(.fixture(.script))
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()

        do {
            _ = try await context.eval(EcritumScript("fixture:error", language: .clojure, sourceName: "error.clj"))
            XCTFail("expected eval to throw")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .script)
        }

        XCTAssertTrue(adapter.events.contains("jobResult:200"))
        XCTAssertTrue(adapter.events.contains("jobDestroy:200"))
        XCTAssertFalse(adapter.events.contains { $0.hasPrefix("valueDestroy:") })
    }

    func testTaskCancellationCallsNativeCancelAndDestroysJob() async throws {
        let adapter = FakeEvalABI()
        adapter.waitStates = [.running, .cancelled]
        adapter.blockFirstWaitForMicroseconds = 50_000
        adapter.resultError = .cancelled(.fixture(.cancelled))
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()
        let task = Task {
            try await context.eval(EcritumScript("fixture:pending", language: .clojure, sourceName: "pending.clj"))
        }

        try await Task.sleep(nanoseconds: 5_000_000)
        task.cancel()

        do {
            _ = try await task.value
            XCTFail("expected eval cancellation to throw")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .cancelled)
        }

        XCTAssertTrue(adapter.events.contains("jobCancel:200"))
        XCTAssertTrue(adapter.events.contains("jobDestroy:200"))
    }

    func testEvalAfterContextCloseThrowsClosedWithoutStartingJob() async throws {
        let adapter = FakeEvalABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()
        try context.close()

        do {
            _ = try await context.eval(EcritumScript("fixture:int:42", language: .clojure))
            XCTFail("expected closed context")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .closed)
        }

        XCTAssertFalse(adapter.events.contains { $0.hasPrefix("evalStart") })
    }

    func testArtifactBackedClojureEvalReturnsNativeValue() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        let value = try await context.eval(EcritumScript(
            "{\"answer\" (+ 40 2) \"items\" [1 2 3] \"nil\" nil \"flag\" true \"ratio\" 3.5 \"text\" \"hello\"}",
            language: .clojure,
            sourceName: "swift-artifact-smoke.clj"
        ))

        XCTAssertEqual(value, .object([
            "answer": .int(42),
            "items": .array([.int(1), .int(2), .int(3)]),
            "nil": .null,
            "flag": .bool(true),
            "ratio": .double(3.5),
            "text": .string("hello"),
        ]))
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedClojureEvalMapsScriptErrorDetails() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        do {
            _ = try await context.eval(EcritumScript("(/ 1 0)", language: .clojure, sourceName: "swift-error.clj"))
            XCTFail("expected eval to throw")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .script)
            XCTAssertEqual(error.category, .runtime)
            XCTAssertEqual(error.details?.operation, "eval")
            XCTAssertEqual(error.details?.language, "clojure")
            XCTAssertEqual(error.details?.sourceName, "swift-error.clj")
        }
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedClojureStandardLibraryPureFacadesAndDefaultDenials() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        let json = try await context.eval(EcritumScript(
            "(ecritum.json/write-string {\"b\" 2 \"a\" 1})",
            language: .clojure,
            sourceName: "swift-facade-json.clj"
        ))
        XCTAssertEqual(json, .string("{\"a\":1,\"b\":2}"))

        let time = try await context.eval(EcritumScript(
            "(ecritum.time/format-instant (ecritum.time/parse-instant \"2026-06-05T00:00:00Z\"))",
            language: .clojure,
            sourceName: "swift-facade-time.clj"
        ))
        XCTAssertEqual(time, .string("2026-06-05T00:00:00Z"))

        do {
            _ = try await context.eval(EcritumScript(
                "(ecritum.time/now)",
                language: .clojure,
                sourceName: "swift-time-denied.clj"
            ))
            XCTFail("expected time permission denial")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.sourceName, "swift-time-denied.clj")
        }

        do {
            _ = try await context.eval(EcritumScript(
                "(ecritum.http/request {\"url\" \"https://example.com\"})",
                language: .clojure,
                sourceName: "swift-http-denied.clj"
            ))
            XCTFail("expected http permission denial")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.sourceName, "swift-http-denied.clj")
        }
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedClojureFilesystemFacadeUsesConfiguredRoot() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let fileManager = FileManager.default
        let root = URL(fileURLWithPath: "/tmp/ecritum-swift-facades-\(UUID().uuidString)", isDirectory: true)
        let outside = URL(fileURLWithPath: "/tmp/ecritum-swift-facades-outside-\(UUID().uuidString).txt")
        try fileManager.createDirectory(at: root, withIntermediateDirectories: false)
        defer {
            try? fileManager.removeItem(at: root)
            try? fileManager.removeItem(at: outside)
        }
        let inside = root.appendingPathComponent("inside.txt")
        try "swift-data".write(to: inside, atomically: true, encoding: .utf8)
        try "outside-data".write(to: outside, atomically: true, encoding: .utf8)

        let runtime = try EcritumRuntime(.init(
            languages: [.clojure],
            policy: .defaultDeny.withFilesystem(.readOnly(roots: [try .directory(root)]))
        ))
        let context = try runtime.context()

        let readText = try await context.eval(EcritumScript(
            "(ecritum.fs/read-text \"\(inside.path)\")",
            language: .clojure,
            sourceName: "swift-fs-read.clj"
        ))
        XCTAssertEqual(readText, .string("swift-data"))

        let readBytes = try await context.eval(EcritumScript(
            "(ecritum.fs/read-bytes \"\(inside.path)\")",
            language: .clojure,
            sourceName: "swift-fs-bytes.clj"
        ))
        XCTAssertEqual(readBytes, .data(Data("swift-data".utf8)))

        do {
            _ = try await context.eval(EcritumScript(
                "(ecritum.fs/read-text \"\(outside.path)\")",
                language: .clojure,
                sourceName: "swift-fs-outside.clj"
            ))
            XCTFail("expected outside-root permission denial")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.sourceName, "swift-fs-outside.clj")
        }
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedClojureEvalCallsRegisteredSwiftHostFunctions() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let namespace = try runtime.namespace(.init("app"))
        try namespace.register(.init("answer")) { call in
            XCTAssertEqual(try call.argumentCount(), 0)
            return .int(42)
        }
        try namespace.register(.init("add_one")) { call in
            XCTAssertEqual(try call.argumentCount(), 1)
            XCTAssertEqual(try call.value(at: 0), .int(41))
            return .int(42)
        }
        try namespace.register(.init("blob")) { _ in
            .data(Data([0, 1, 2, 255]))
        }
        let context = try runtime.context()

        let value = try await context.eval(EcritumScript(
            "{\"answer\" (app/answer) \"plus\" (app/add_one 41) \"blob\" (app/blob)}",
            language: .clojure,
            sourceName: "swift-host-success.clj"
        ))

        XCTAssertEqual(value, .object([
            "answer": .int(42),
            "plus": .int(42),
            "blob": .data(Data([0, 1, 2, 255])),
        ]))
        _ = namespace
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedClojureEvalMapsSwiftHostCallbackFailure() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let namespace = try runtime.namespace(.init("app"))
        try namespace.register(.init("fail")) { _ in
            throw EcritumError.callback(EcritumErrorDetails(
                status: .callback,
                category: .callback,
                message: "token=SECRET",
                operation: "host_callback"
            ))
        }
        let context = try runtime.context()

        do {
            _ = try await context.eval(EcritumScript("(app/fail)", language: .clojure, sourceName: "swift-host-fail.clj"))
            XCTFail("expected callback failure")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .callback)
            XCTAssertEqual(error.category, .callback)
            XCTAssertEqual(error.details?.operation, "eval")
            XCTAssertEqual(error.details?.language, "clojure")
            XCTAssertEqual(error.details?.sourceName, "swift-host-fail.clj")
            XCTAssertFalse(error.details?.message.contains("SECRET") ?? false)
        }
        _ = namespace
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedJavaScriptEvalReturnsNativeValuesAndErrors() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        let value = try await context.eval(EcritumScript(
            "({answer: 40 + 2, items: [1, 'two', true], nil: null, flag: true, ratio: 3.5, text: 'hello', blob: new Uint8Array([0, 1, 2, 255])})",
            language: .javascript,
            sourceName: "swift-artifact-smoke.js"
        ))

        XCTAssertEqual(value, .object([
            "answer": .int(42),
            "items": .array([.int(1), .string("two"), .bool(true)]),
            "nil": .null,
            "flag": .bool(true),
            "ratio": .double(3.5),
            "text": .string("hello"),
            "blob": .data(Data([0, 1, 2, 255])),
        ]))

        do {
            _ = try await context.eval(EcritumScript("throw new Error('boom')", language: .javascript, sourceName: "swift-error.js"))
            XCTFail("expected eval to throw")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .script)
            XCTAssertEqual(error.category, .runtime)
            XCTAssertEqual(error.details?.language, "javascript")
            XCTAssertEqual(error.details?.sourceName, "swift-error.js")
        }

        do {
            _ = try await context.eval(EcritumScript("(async function(){ return 42; })()", language: .javascript, sourceName: "swift-promise.js"))
            XCTFail("expected promise rejection")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .script)
            XCTAssertEqual(error.category, .runtime)
            XCTAssertEqual(error.details?.sourceName, "swift-promise.js")
        }

        do {
            _ = try await context.eval(EcritumScript("await 42", language: .javascript, sourceName: "swift-await.js"))
            XCTFail("expected top-level await syntax error")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .script)
            XCTAssertEqual(error.category, .syntax)
            XCTAssertEqual(error.details?.sourceName, "swift-await.js")
        }
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedJavaScriptEvalCallsRegisteredSwiftHostFunctions() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let app = try runtime.namespace(.init("app"))
        try app.register(.init("answer")) { call in
            XCTAssertEqual(try call.argumentCount(), 0)
            return .int(42)
        }
        try app.register(.init("blob")) { _ in
            .data(Data([0, 1, 2, 255]))
        }
        try app.register(.init("fail")) { _ in
            throw EcritumError.callback(EcritumErrorDetails(
                status: .callback,
                category: .callback,
                message: "token=SECRET",
                operation: "host_callback"
            ))
        }
        let tools = try runtime.namespace(.init("app.tools"))
        try tools.register(.init("notify")) { call in
            XCTAssertEqual(try call.argumentCount(), 3)
            XCTAssertEqual(try call.value(at: 0), .int(41))
            XCTAssertEqual(try call.value(at: 1), .string("done"))
            XCTAssertEqual(try call.value(at: 2), .object(["ok": .bool(true)]))
            return .array([.int(42), .string("done")])
        }
        let context = try runtime.context()

        let answer = try await context.eval(EcritumScript(
            "ecritum.app.answer()",
            language: .javascript,
            sourceName: "swift-host-success.js"
        ))
        XCTAssertEqual(answer, .int(42))

        let notify = try await context.eval(EcritumScript(
            "ecritum.app.tools.notify(41, 'done', {ok: true})",
            language: .javascript,
            sourceName: "swift-host-notify.js"
        ))
        XCTAssertEqual(notify, .array([.int(42), .string("done")]))

        let blob = try await context.eval(EcritumScript(
            "ecritum.app.blob()",
            language: .javascript,
            sourceName: "swift-host-blob.js"
        ))
        XCTAssertEqual(blob, .data(Data([0, 1, 2, 255])))

        do {
            _ = try await context.eval(EcritumScript("ecritum.app.fail()", language: .javascript, sourceName: "swift-host-fail.js"))
            XCTFail("expected callback failure")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .callback)
            XCTAssertEqual(error.category, .callback)
            XCTAssertEqual(error.details?.operation, "eval")
            XCTAssertEqual(error.details?.language, "javascript")
            XCTAssertEqual(error.details?.sourceName, "swift-host-fail.js")
            XCTAssertFalse(error.details?.message.contains("SECRET") ?? false)
        }
        _ = app
        _ = tools
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }

    func testArtifactBackedJavaScriptStandardLibraryFacadesAndDefaultDenials() async throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        let json = try await context.eval(EcritumScript(
            "ecritum.json.writeString({b: 2, a: 1})",
            language: .javascript,
            sourceName: "swift-js-facade-json.js"
        ))
        XCTAssertEqual(json, .string("{\"a\":1,\"b\":2}"))

        let time = try await context.eval(EcritumScript(
            "ecritum.time.formatInstant(ecritum.time.parseInstant('2026-06-05T00:00:00Z'))",
            language: .javascript,
            sourceName: "swift-js-facade-time.js"
        ))
        XCTAssertEqual(time, .string("2026-06-05T00:00:00Z"))

        do {
            _ = try await context.eval(EcritumScript("ecritum.time.now()", language: .javascript, sourceName: "swift-js-time-denied.js"))
            XCTFail("expected time permission denial")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.sourceName, "swift-js-time-denied.js")
        }

        do {
            _ = try await context.eval(EcritumScript("ecritum.http.request({url: 'https://example.com'})", language: .javascript, sourceName: "swift-js-http-denied.js"))
            XCTFail("expected http permission denial")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.sourceName, "swift-js-http-denied.js")
        }

        let collisionRuntime = try EcritumRuntime()
        let namespace = try collisionRuntime.namespace(.init("json"))
        try namespace.register(.init("custom")) { _ in .int(1) }
        let collisionContext = try collisionRuntime.context()
        do {
            _ = try await collisionContext.eval(EcritumScript("1", language: .javascript, sourceName: "swift-js-collision.js"))
            XCTFail("expected reserved namespace collision")
        } catch let error as EcritumError {
            XCTAssertEqual(error.status, .permissionDenied)
            XCTAssertEqual(error.category, .permission)
            XCTAssertEqual(error.details?.language, "javascript")
            XCTAssertEqual(error.details?.sourceName, "swift-js-collision.js")
        }
        _ = namespace
        #else
        throw XCTSkip("requires local runtime artifact")
        #endif
    }
}

private final class FakeEvalABI: EcritumLifecycleABI, EcritumEvalABI {
    var nextRuntime: ecritum_runtime_t = 1
    var nextContext: ecritum_context_t = 100
    var nextJob: ecritum_job_t = 200
    var nextValue: ecritum_value_t = 500
    var runtimeContexts: [ecritum_runtime_t: Set<ecritum_context_t>] = [:]
    var contextRuntime: [ecritum_context_t: ecritum_runtime_t] = [:]
    var waitStates: [EcritumJobState] = []
    var waitTimeouts: [UInt64] = []
    var copiedValues: [ecritum_value_t: EcritumValue] = [:]
    var resultError: EcritumError?
    var blockFirstWaitForMicroseconds: useconds_t = 0
    private(set) var events: [String] = []

    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t {
        events.append("runtimeCreate")
        let handle = nextRuntime
        nextRuntime += 1
        runtimeContexts[handle] = []
        return handle
    }

    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws {
        guard handle != 0 else { return }
        events.append("runtimeDestroy")
        runtimeContexts[handle] = nil
        handle = 0
    }

    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t {
        events.append("contextCreate")
        let handle = nextContext
        nextContext += 1
        runtimeContexts[runtime, default: []].insert(handle)
        contextRuntime[handle] = runtime
        return handle
    }

    func contextDestroy(_ handle: inout ecritum_context_t) throws {
        guard handle != 0 else { return }
        events.append("contextDestroy")
        if let runtime = contextRuntime.removeValue(forKey: handle) {
            runtimeContexts[runtime]?.remove(handle)
        }
        handle = 0
    }

    func namespaceCreate(runtime: ecritum_runtime_t, name: EcritumNamespace.Name) throws -> ecritum_namespace_t {
        throw EcritumError.runtimeArtifactMissing
    }

    func namespaceDestroy(_ handle: inout ecritum_namespace_t) throws {
        handle = 0
    }

    func registerFunction(namespace: ecritum_namespace_t, name: EcritumFunctionName, callback: @escaping EcritumHostFunction) throws -> ecritum_function_t {
        throw EcritumError.runtimeArtifactMissing
    }

    func functionDestroy(_ handle: inout ecritum_function_t) throws {
        handle = 0
    }

    func evalStart(context: ecritum_context_t, script: EcritumScript) throws -> ecritum_job_t {
        events.append("evalStart:\(script.language.rawValue):\(script.sourceName ?? "")")
        let handle = nextJob
        nextJob += 1
        return handle
    }

    func jobWait(_ job: ecritum_job_t, timeoutNanoseconds: UInt64) throws -> EcritumJobState {
        events.append("jobWait:\(job)")
        waitTimeouts.append(timeoutNanoseconds)
        if blockFirstWaitForMicroseconds > 0 {
            usleep(blockFirstWaitForMicroseconds)
            blockFirstWaitForMicroseconds = 0
        }
        if waitStates.isEmpty {
            return .succeeded
        }
        return waitStates.removeFirst()
    }

    func jobCancel(_ job: ecritum_job_t) throws {
        events.append("jobCancel:\(job)")
    }

    func jobResult(_ job: ecritum_job_t) throws -> ecritum_value_t {
        events.append("jobResult:\(job)")
        if let resultError {
            throw resultError
        }
        return nextValue
    }

    func jobDestroy(_ job: inout ecritum_job_t) throws {
        events.append("jobDestroy:\(job)")
        job = 0
    }

    func copyValue(_ value: ecritum_value_t) throws -> EcritumValue {
        events.append("copyValue:\(value)")
        return copiedValues[value] ?? .null
    }

    func valueDestroy(_ value: inout ecritum_value_t) {
        events.append("valueDestroy:\(value)")
        value = 0
    }
}

private extension EcritumErrorDetails {
    static func fixture(_ status: EcritumStatus) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "\(status) failed",
            operation: "eval"
        )
    }
}
