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

    func testCallbackReadsArgumentsThroughFakeAdapter() throws {
        let adapter = FakeHostRegistrationABI()
        let runtime = try EcritumRuntime(adapter: adapter)
        let namespace = try runtime.namespace(.init("app"))

        try namespace.register(.init("combine")) { call in
            XCTAssertEqual(try call.argumentCount(), 2)
            XCTAssertEqual(try call.value(at: 0), .int(41))
            XCTAssertEqual(try call.value(at: 1), .string("done"))
            XCTAssertThrowsError(try call.value(at: 2)) { error in
                XCTAssertEqual((error as? EcritumError)?.status, .invalidArgument)
            }
            XCTAssertThrowsError(try call.value(at: -1)) { error in
                XCTAssertEqual((error as? EcritumError)?.status, .invalidArgument)
            }
            return .array([try call.value(at: 0), try call.value(at: 1)])
        }

        XCTAssertEqual(
            try adapter.invoke(functionNamed: "combine", arguments: [.int(41), .string("done")]),
            .array([.int(41), .string("done")])
        )
    }

    func testArtifactBackedCallValueRejectsInvalidHandle() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let call = EcritumCall(handle: 0, adapter: EcritumCLifecycleABI.shared)
        XCTAssertThrowsError(try call.argumentCount()) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidHandle)
        }
        XCTAssertThrowsError(try call.value(at: 0)) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidHandle)
        }
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed call access.")
        #endif
    }

    func testArtifactBackedNativeValueConstructionCleansUpAfterNestedFailure() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let adapter = EcritumCLifecycleABI.shared
        let oversized = String(repeating: "x", count: 4_097)
        XCTAssertThrowsError(try adapter.makeNativeValue(.array([.int(1), .string(oversized)]))) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .inputTooLarge)
        }

        var values: [ecritum_value_t] = []
        values.reserveCapacity(4_095)
        defer {
            for index in values.indices {
                adapter.valueDestroy(&values[index])
            }
        }
        for _ in 0..<4_095 {
            values.append(try adapter.makeNativeValue(.null))
        }
        XCTAssertThrowsError(try adapter.makeNativeValue(.null)) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .outOfMemory)
        }
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed value cleanup.")
        #endif
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

private final class FakeHostRegistrationABI: EcritumLifecycleABI, EcritumCallABI {
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
    var nextCall: ecritum_call_t = 10_000
    var nextValue: ecritum_value_t = 20_000
    var liveRuntimes: Set<ecritum_runtime_t> = []
    var namespaces: [ecritum_namespace_t: NamespaceRecord] = [:]
    var functions: [ecritum_function_t: FunctionRecord] = [:]
    var calls: [ecritum_call_t: [EcritumValue]] = [:]
    var values: [ecritum_value_t: EcritumValue] = [:]
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

    func callArgumentCount(_ call: ecritum_call_t) throws -> Int {
        guard let arguments = calls[call] else {
            throw EcritumError.invalidHandle(.fixture(.invalidHandle))
        }
        return arguments.count
    }

    func callArgument(_ call: ecritum_call_t, index: Int) throws -> ecritum_value_t {
        guard let arguments = calls[call] else {
            throw EcritumError.invalidHandle(.fixture(.invalidHandle))
        }
        guard index >= 0, index < arguments.count else {
            throw EcritumError.invalidArgument(.fixture(.invalidArgument))
        }
        let handle = nextValue
        nextValue += 1
        values[handle] = arguments[index]
        return handle
    }

    func copyValue(_ value: ecritum_value_t) throws -> EcritumValue {
        guard let copied = values[value] else {
            throw EcritumError.invalidHandle(.fixture(.invalidHandle))
        }
        return copied
    }

    func valueDestroy(_ value: inout ecritum_value_t) {
        values.removeValue(forKey: value)
        value = 0
    }

    func invoke(functionNamed name: String, arguments: [EcritumValue] = []) throws -> EcritumValue {
        guard let function = functions.values.first(where: { $0.name == name }) else {
            throw EcritumError.invalidHandle(.fixture(.invalidHandle))
        }
        let call = nextCall
        nextCall += 1
        calls[call] = arguments
        defer { calls.removeValue(forKey: call) }
        return try function.callback(EcritumCall(handle: call, adapter: self))
    }
}

private extension EcritumCall {
    static func fixture(arguments: [EcritumValue] = []) -> EcritumCall {
        EcritumCall(fixtureArguments: arguments)
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
