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
