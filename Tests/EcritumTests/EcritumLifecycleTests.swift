import CEcritum
import XCTest
@testable import Ecritum

final class EcritumLifecycleTests: XCTestCase {
    func testRuntimeInitReportsMissingArtifactInScaffoldMode() {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        return
        #else
        XCTAssertThrowsError(try EcritumRuntime()) { error in
            XCTAssertEqual(error as? EcritumError, .runtimeArtifactMissing)
        }
        #endif
    }

    func testArtifactBackedRuntimeContextLifecycle() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let context = try runtime.context()

        XCTAssertThrowsError(try runtime.close()) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .contextsAlive)
        }

        try context.close()
        try runtime.close()
        try runtime.close()
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed lifecycle.")
        #endif
    }

    func testRuntimeCloseDestroysHandleExactlyOnce() throws {
        let adapter = FakeLifecycleABI()
        let runtime = try EcritumRuntime(adapter: adapter)

        try runtime.close()
        try runtime.close()

        XCTAssertEqual(adapter.runtimeDestroyCalls, 1)
        XCTAssertEqual(adapter.destroyedRuntimes, [1])
    }

    func testRuntimeDeinitClosesBestEffort() throws {
        let adapter = FakeLifecycleABI()

        do {
            _ = try EcritumRuntime(adapter: adapter)
        }

        XCTAssertEqual(adapter.runtimeDestroyCalls, 1)
        XCTAssertEqual(adapter.destroyedRuntimes, [1])
    }

    func testContextRetainsParentRuntimeUntilContextIsReleased() throws {
        let adapter = FakeLifecycleABI()
        var runtime: EcritumRuntime? = try EcritumRuntime(adapter: adapter)
        let weakRuntime = WeakBox(runtime)
        var context: EcritumContext? = try runtime?.context()

        runtime = nil
        XCTAssertNotNil(weakRuntime.value)

        try context?.close()
        context = nil
        XCTAssertNil(weakRuntime.value)
        XCTAssertEqual(adapter.contextDestroyCalls, 1)
        XCTAssertEqual(adapter.runtimeDestroyCalls, 1)
    }

    func testRuntimeCloseWithLiveContextThrowsAndLeavesRuntimeOpen() throws {
        let adapter = FakeLifecycleABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()

        XCTAssertThrowsError(try runtime.close()) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .contextsAlive)
        }

        _ = try runtime.context()
        try context.close()
        try runtime.close()

        XCTAssertEqual(adapter.runtimeDestroyCalls, 2)
        XCTAssertEqual(adapter.contextDestroyCalls, 2)
    }

    func testPostCloseContextCreationThrowsClosed() throws {
        let runtime = try EcritumRuntime(adapter: FakeLifecycleABI())

        try runtime.close()

        XCTAssertThrowsError(try runtime.context()) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .closed)
        }
    }

    func testContextCloseIsIdempotent() throws {
        let adapter = FakeLifecycleABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let context = try runtime.context()

        try context.close()
        try context.close()
        try runtime.close()

        XCTAssertEqual(adapter.contextDestroyCalls, 1)
    }

    func testTeardownFailureThrowsAndSecondCloseIsNoOp() throws {
        let adapter = FakeLifecycleABI()
        adapter.runtimeDestroyFailures[1] = .teardownFailed(.fixture(.teardownFailed))
        let runtime = try EcritumRuntime(adapter: adapter)

        XCTAssertThrowsError(try runtime.close()) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .teardownFailed)
        }

        try runtime.close()
        XCTAssertEqual(adapter.runtimeDestroyCalls, 1)
    }

    func testAdapterCopiesErrorDetailsBeforeDestroy() throws {
        let adapter = FakeLifecycleABI()
        adapter.runtimeDestroyFailures[1] = .contextsAlive(.fixture(.contextsAlive))
        let runtime = try EcritumRuntime(adapter: adapter)

        XCTAssertThrowsError(try runtime.close()) { error in
            let ecritumError = error as? EcritumError
            XCTAssertEqual(ecritumError?.status, .contextsAlive)
            XCTAssertEqual(ecritumError?.details?.message, "contextsAlive failed")
        }
        XCTAssertEqual(adapter.errorDestroyCalls, 1)
    }
}

private final class WeakBox<T: AnyObject> {
    weak var value: T?

    init(_ value: T?) {
        self.value = value
    }
}

private final class FakeLifecycleABI: EcritumLifecycleABI {
    var nextRuntime: ecritum_runtime_t = 1
    var nextContext: ecritum_context_t = 100
    var liveContexts: [ecritum_runtime_t: Int] = [:]
    var contextParents: [ecritum_context_t: ecritum_runtime_t] = [:]
    var runtimeDestroyFailures: [ecritum_runtime_t: EcritumError] = [:]
    var runtimeDestroyCalls = 0
    var contextDestroyCalls = 0
    var errorDestroyCalls = 0
    var destroyedRuntimes: [ecritum_runtime_t] = []

    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t {
        let handle = nextRuntime
        nextRuntime += 1
        liveContexts[handle] = 0
        return handle
    }

    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws {
        guard handle != 0 else {
            return
        }

        runtimeDestroyCalls += 1
        if let failure = runtimeDestroyFailures[handle] {
            errorDestroyCalls += 1
            if failure.status == .teardownFailed {
                handle = 0
            }
            throw failure
        }

        if liveContexts[handle, default: 0] > 0 {
            errorDestroyCalls += 1
            throw EcritumError.contextsAlive(.fixture(.contextsAlive))
        }

        destroyedRuntimes.append(handle)
        liveContexts[handle] = nil
        handle = 0
    }

    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t {
        guard runtime != 0 else {
            throw EcritumError.closed(.fixture(.closed))
        }

        let handle = nextContext
        nextContext += 1
        liveContexts[runtime, default: 0] += 1
        contextParents[handle] = runtime
        return handle
    }

    func contextDestroy(_ handle: inout ecritum_context_t) throws {
        guard handle != 0 else {
            return
        }

        contextDestroyCalls += 1
        if let runtime = contextParents.removeValue(forKey: handle) {
            liveContexts[runtime, default: 1] -= 1
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
}

private extension EcritumErrorDetails {
    static func fixture(_ status: EcritumStatus) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "\(status) failed",
            operation: "lifecycle"
        )
    }
}
