import CEcritum
import XCTest
@testable import Ecritum

final class EcritumHostRegistrationTests: XCTestCase {
    func testNamespaceNameValidation() throws {
        XCTAssertEqual(try EcritumNamespace.Name("app").rawValue, "app")
        XCTAssertEqual(try EcritumNamespace.Name("app.tools").rawValue, "app.tools")
        XCTAssertThrowsError(try EcritumNamespace.Name(""))
        XCTAssertThrowsError(try EcritumNamespace.Name("app."))
        XCTAssertThrowsError(try EcritumNamespace.Name(".app"))
        XCTAssertThrowsError(try EcritumNamespace.Name("app..tools"))
        XCTAssertThrowsError(try EcritumNamespace.Name("1app"))
        XCTAssertThrowsError(try EcritumNamespace.Name("app-tools"))
        XCTAssertThrowsError(try EcritumNamespace.Name("java.tools"))
        XCTAssertThrowsError(try EcritumNamespace.Name("Graal.tools"))
        XCTAssertThrowsError(try EcritumNamespace.Name(String(repeating: "a", count: 256)))
    }

    func testFunctionNameValidation() throws {
        XCTAssertEqual(try EcritumFunctionName("notify").rawValue, "notify")
        XCTAssertEqual(try EcritumFunctionName("replaceSelection_1").rawValue, "replaceSelection_1")
        XCTAssertThrowsError(try EcritumFunctionName(""))
        XCTAssertThrowsError(try EcritumFunctionName("1notify"))
        XCTAssertThrowsError(try EcritumFunctionName("notify-user"))
        XCTAssertThrowsError(try EcritumFunctionName("notify.user"))
        XCTAssertThrowsError(try EcritumFunctionName("ecritum"))
        XCTAssertThrowsError(try EcritumFunctionName("TRUFFLE"))
        XCTAssertThrowsError(try EcritumFunctionName(String(repeating: "a", count: 256)))
    }

    func testRuntimeCreatesNamespaceAndRegistersFunction() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let namespace = try runtime.namespace(.init("app"))

        try namespace.register(.init("notify")) { _ in .null }

        XCTAssertEqual(adapter.createdNamespaces.map(\.name), ["app"])
        XCTAssertEqual(adapter.registeredFunctions.map(\.name), ["notify"])
        XCTAssertEqual(adapter.functionDestroyCalls, 0)
        try runtime.close()
        XCTAssertEqual(adapter.namespaceDestroyCalls, 1)
        XCTAssertEqual(adapter.functionDestroyCalls, 1)
    }

    func testDuplicateRegistrationThrowsAlreadyExists() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let namespace = try runtime.namespace(.init("app"))

        try namespace.register(.init("notify")) { _ in .null }

        XCTAssertThrowsError(try namespace.register(.init("notify")) { _ in .null }) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .alreadyExists)
        }
        try runtime.close()
    }

    func testRegistrationAfterRuntimeCloseThrowsClosed() throws {
        let runtime = try EcritumRuntime(adapter: FakeHostRegistrationABI())
        let namespace = try runtime.namespace(.init("app"))

        try runtime.close()

        XCTAssertThrowsError(try namespace.register(.init("notify")) { _ in .null }) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .closed)
        }
    }

    func testNamespaceCloseAfterRuntimeCloseIsNoOp() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let namespace = try runtime.namespace(.init("app"))
        try namespace.register(.init("notify")) { _ in .null }

        try runtime.close()
        try namespace.close()

        XCTAssertEqual(adapter.namespaceDestroyCalls, 1)
        XCTAssertEqual(adapter.functionDestroyCalls, 1)
    }

    func testRuntimeCloseOnlyDestroysOwnedNamespaces() throws {
        let adapter = FakeHostRegistrationABI()
        let firstRuntime = try EcritumRuntime(adapter: adapter)
        let secondRuntime = try EcritumRuntime(adapter: adapter)
        let firstNamespace = try firstRuntime.namespace(.init("first"))
        let secondNamespace = try secondRuntime.namespace(.init("second"))

        try firstRuntime.close()
        XCTAssertEqual(adapter.namespaceDestroyCalls, 1)

        try secondRuntime.close()
        XCTAssertEqual(adapter.namespaceDestroyCalls, 2)
        _ = firstNamespace
        _ = secondNamespace
    }

    func testNamespaceDeinitDestroysRegisteredFunctionsExactlyOnce() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)

        do {
            let namespace = try runtime.namespace(.init("app"))
            try namespace.register(.init("notify")) { _ in .null }
            _ = namespace
        }

        XCTAssertEqual(adapter.namespaceDestroyCalls, 1)
        XCTAssertEqual(adapter.functionDestroyCalls, 1)
        try runtime.close()
        XCTAssertEqual(adapter.namespaceDestroyCalls, 1)
        XCTAssertEqual(adapter.functionDestroyCalls, 1)
    }

    func testCallbackSuccessAndFailureThroughFakeAdapter() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let namespace = try runtime.namespace(.init("app"))

        try namespace.register(.init("ok")) { _ in .string("done") }
        XCTAssertEqual(try adapter.invoke(functionNamed: "ok"), .string("done"))

        try namespace.register(.init("fail")) { _ in
            throw EcritumError.callback(.fixture(.callback))
        }
        XCTAssertThrowsError(try adapter.invoke(functionNamed: "fail")) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .callback)
        }
    }

    func testArtifactBackedNamespaceAndFunctionRegistration() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let namespace = try runtime.namespace(.init("app"))

        XCTAssertThrowsError(try runtime.namespace(.init("app"))) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .alreadyExists)
        }

        try namespace.register(.init("notify")) { _ in .null }
        XCTAssertThrowsError(try namespace.register(.init("notify")) { _ in .null }) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .alreadyExists)
        }

        try namespace.close()
        try runtime.close()
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed host registration.")
        #endif
    }

    func testArtifactBackedFailedRegistrationReleasesCallbackBox() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime()
        let namespace = try runtime.namespace(.init("app"))
        try namespace.register(.init("notify")) { _ in .null }

        weak var releasedProbe: CallbackProbe?
        do {
            let probe = CallbackProbe()
            releasedProbe = probe
            XCTAssertThrowsError(try namespace.register(.init("notify")) { [probe] _ in
                _ = probe
                return .null
            }) { error in
                XCTAssertEqual((error as? EcritumError)?.status, .alreadyExists)
            }
        }
        XCTAssertNil(releasedProbe)

        try namespace.close()
        try runtime.close()
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed callback-box release.")
        #endif
    }
}

private final class CallbackProbe {}

private final class FakeHostRegistrationABI: EcritumLifecycleABI {
    struct NamespaceRecord {
        let handle: ecritum_namespace_t
        let runtime: ecritum_runtime_t
        let name: String
    }

    struct FunctionRecord {
        let handle: ecritum_function_t
        let namespace: ecritum_namespace_t
        let name: String
        let callback: EcritumHostFunction
    }

    var nextRuntime: ecritum_runtime_t = 1
    var nextNamespace: ecritum_namespace_t = 100
    var nextFunction: ecritum_function_t = 1_000
    var liveRuntimes: Set<ecritum_runtime_t> = []
    var namespaces: [ecritum_namespace_t: NamespaceRecord] = [:]
    var functions: [ecritum_function_t: FunctionRecord] = [:]
    var createdNamespaces: [NamespaceRecord] = []
    var registeredFunctions: [FunctionRecord] = []
    var namespaceDestroyCalls = 0
    var functionDestroyCalls = 0

    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t {
        let handle = nextRuntime
        nextRuntime += 1
        liveRuntimes.insert(handle)
        return handle
    }

    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws {
        guard handle != 0 else { return }
        for namespace in namespaces.values.filter({ $0.runtime == handle }).map(\.handle).sorted() {
            var namespaceHandle = namespace
            try namespaceDestroy(&namespaceHandle)
        }
        liveRuntimes.remove(handle)
        handle = 0
    }

    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t {
        0
    }

    func contextDestroy(_ handle: inout ecritum_context_t) throws {}

    func namespaceCreate(runtime: ecritum_runtime_t, name: EcritumNamespace.Name) throws -> ecritum_namespace_t {
        guard liveRuntimes.contains(runtime) else {
            throw EcritumError.closed(.fixture(.closed))
        }
        let handle = nextNamespace
        nextNamespace += 1
        let record = NamespaceRecord(handle: handle, runtime: runtime, name: name.rawValue)
        namespaces[handle] = record
        createdNamespaces.append(record)
        return handle
    }

    func namespaceDestroy(_ handle: inout ecritum_namespace_t) throws {
        guard handle != 0 else { return }
        guard namespaces[handle] != nil else {
            handle = 0
            return
        }
        namespaceDestroyCalls += 1
        for function in functions.values.filter({ $0.namespace == handle }).map(\.handle) {
            var functionHandle = function
            try functionDestroy(&functionHandle)
        }
        namespaces.removeValue(forKey: handle)
        handle = 0
    }

    func registerFunction(namespace: ecritum_namespace_t, name: EcritumFunctionName, callback: @escaping EcritumHostFunction) throws -> ecritum_function_t {
        guard namespaces[namespace] != nil else {
            throw EcritumError.closed(.fixture(.closed))
        }
        if functions.values.contains(where: { $0.namespace == namespace && $0.name == name.rawValue }) {
            throw EcritumError.alreadyExists(.fixture(.alreadyExists))
        }

        let handle = nextFunction
        nextFunction += 1
        let record = FunctionRecord(handle: handle, namespace: namespace, name: name.rawValue, callback: callback)
        functions[handle] = record
        registeredFunctions.append(record)
        return handle
    }

    func functionDestroy(_ handle: inout ecritum_function_t) throws {
        guard handle != 0 else { return }
        functionDestroyCalls += 1
        functions.removeValue(forKey: handle)
        handle = 0
    }

    func invoke(functionNamed name: String) throws -> EcritumValue {
        guard let function = functions.values.first(where: { $0.name == name }) else {
            throw EcritumError.invalidHandle(.fixture(.invalidHandle))
        }
        return try function.callback(EcritumCall.fixture())
    }
}

private extension EcritumCall {
    static func fixture() -> EcritumCall {
        EcritumCall()
    }
}

private extension EcritumErrorDetails {
    static func fixture(_ status: EcritumStatus) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "\(status) failed",
            operation: "host_registration"
        )
    }
}
