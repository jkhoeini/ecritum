import CEcritum
import Foundation

protocol EcritumLifecycleABI: AnyObject {
    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t
    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws
    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t
    func contextDestroy(_ handle: inout ecritum_context_t) throws
    func namespaceCreate(runtime: ecritum_runtime_t, name: EcritumNamespace.Name) throws -> ecritum_namespace_t
    func namespaceDestroy(_ handle: inout ecritum_namespace_t) throws
    func registerFunction(namespace: ecritum_namespace_t, name: EcritumFunctionName, callback: @escaping EcritumHostFunction) throws -> ecritum_function_t
    func functionDestroy(_ handle: inout ecritum_function_t) throws
}

enum EcritumJobState: Int32, Sendable {
    case pending = 0
    case running = 1
    case succeeded = 2
    case failed = 3
    case cancelRequested = 4
    case cancelled = 5
    case timedOut = 6
    case poisoned = 7

    var isTerminal: Bool {
        switch self {
        case .succeeded, .failed, .cancelled, .timedOut, .poisoned:
            return true
        case .pending, .running, .cancelRequested:
            return false
        }
    }
}

protocol EcritumEvalABI: AnyObject {
    func evalStart(context: ecritum_context_t, script: EcritumScript) throws -> ecritum_job_t
    func jobWait(_ job: ecritum_job_t, timeoutNanoseconds: UInt64) throws -> EcritumJobState
    func jobCancel(_ job: ecritum_job_t) throws
    func jobResult(_ job: ecritum_job_t) throws -> ecritum_value_t
    func jobDestroy(_ job: inout ecritum_job_t) throws
    func copyValue(_ value: ecritum_value_t) throws -> EcritumValue
    func valueDestroy(_ value: inout ecritum_value_t)
}

protocol EcritumCallABI: AnyObject {
    func callArgumentCount(_ call: ecritum_call_t) throws -> Int
    func callArgument(_ call: ecritum_call_t, index: Int) throws -> ecritum_value_t
    func copyValue(_ value: ecritum_value_t) throws -> EcritumValue
    func valueDestroy(_ value: inout ecritum_value_t)
}

/// Owns a packaged Ecritum runtime instance.
public final class EcritumRuntime {
    public struct Configuration: Equatable, Sendable {
        public static let `default` = Configuration()

        public var languages: Set<EcritumLanguage>
        public var policy: EcritumPermissionPolicy
        public var diagnostics: EcritumDiagnosticsPolicy
        public var resourceLimits: EcritumResourceLimits

        public init(
            languages: Set<EcritumLanguage> = [],
            policy: EcritumPermissionPolicy = .defaultDeny,
            diagnostics: EcritumDiagnosticsPolicy = .redacted,
            resourceLimits: EcritumResourceLimits = EcritumResourceLimits()
        ) {
            self.languages = languages
            self.policy = policy
            self.diagnostics = diagnostics
            self.resourceLimits = resourceLimits
        }
    }

    private let adapter: EcritumLifecycleABI
    private var handle: ecritum_runtime_t

    public convenience init(_ configuration: Configuration = .default) throws {
        try self.init(configuration: configuration, adapter: EcritumCLifecycleABI.shared)
    }

    init(configuration: Configuration = .default, adapter: EcritumLifecycleABI) throws {
        self.adapter = adapter
        self.handle = try adapter.runtimeCreate(configuration: configuration)
    }

    deinit {
        try? close()
    }

    var isOpen: Bool {
        handle != 0
    }

    public func context(_ configuration: EcritumContext.Configuration = .default) throws -> EcritumContext {
        guard handle != 0 else {
            throw EcritumError.closed(.lifecycle(.closed, operation: "context_create"))
        }

        let contextHandle = try adapter.contextCreate(runtime: handle, configuration: configuration)
        return EcritumContext(parent: self, handle: contextHandle, adapter: adapter)
    }

    public func namespace(_ name: EcritumNamespace.Name) throws -> EcritumNamespace {
        guard handle != 0 else {
            throw EcritumError.closed(.lifecycle(.closed, operation: "namespace_create"))
        }

        let namespaceHandle = try adapter.namespaceCreate(runtime: handle, name: name)
        return EcritumNamespace(parent: self, handle: namespaceHandle, adapter: adapter)
    }

    public func close() throws {
        guard handle != 0 else {
            return
        }

        try adapter.runtimeDestroy(&handle)
    }
}

/// Owns an Ecritum execution context under a runtime.
public final class EcritumContext {
    public struct Configuration: Equatable, Sendable {
        public static let `default` = Configuration()

        public var policy: EcritumPermissionPolicy.Narrowing
        public var resourceLimits: EcritumResourceLimits.Narrowing

        public init(
            policy: EcritumPermissionPolicy.Narrowing = EcritumPermissionPolicy.Narrowing(),
            resourceLimits: EcritumResourceLimits.Narrowing = EcritumResourceLimits.Narrowing()
        ) {
            self.policy = policy
            self.resourceLimits = resourceLimits
        }
    }

    private let parent: EcritumRuntime
    private let adapter: EcritumLifecycleABI
    private var handle: ecritum_context_t
    private static let jobWaitIntervalNanoseconds: UInt64 = 50_000_000

    init(parent: EcritumRuntime, handle: ecritum_context_t, adapter: EcritumLifecycleABI) {
        self.parent = parent
        self.handle = handle
        self.adapter = adapter
    }

    deinit {
        try? close()
    }

    public func close() throws {
        guard handle != 0 else {
            return
        }

        try adapter.contextDestroy(&handle)
    }

    public func eval(_ script: EcritumScript) async throws -> EcritumValue {
        guard handle != 0 else {
            throw EcritumError.closed(.lifecycle(.closed, operation: "eval"))
        }
        guard let evalAdapter = adapter as? EcritumEvalABI else {
            throw EcritumError.runtimeArtifactMissing
        }

        let cancellation = EcritumJobCancellationBox()
        var job: ecritum_job_t = 0
        return try await withTaskCancellationHandler {
            job = try evalAdapter.evalStart(context: handle, script: script)
            cancellation.set(adapter: evalAdapter, job: job)
            defer {
                try? evalAdapter.jobDestroy(&job)
                cancellation.clear()
            }

            while true {
                if Task.isCancelled {
                    try? evalAdapter.jobCancel(job)
                }

                let state = try evalAdapter.jobWait(job, timeoutNanoseconds: Self.jobWaitIntervalNanoseconds)
                switch state {
                case .succeeded:
                    var nativeValue = try evalAdapter.jobResult(job)
                    defer { evalAdapter.valueDestroy(&nativeValue) }
                    return try evalAdapter.copyValue(nativeValue)
                case .failed, .cancelled, .timedOut, .poisoned:
                    var nativeValue: ecritum_value_t = 0
                    do {
                        nativeValue = try evalAdapter.jobResult(job)
                    } catch {
                        throw error
                    }
                    evalAdapter.valueDestroy(&nativeValue)
                    throw EcritumError.internalFailure(.lifecycle(.internalFailure, operation: "eval"))
                case .pending, .running, .cancelRequested:
                    do {
                        try await Task.sleep(nanoseconds: 1_000_000)
                    } catch is CancellationError {
                        try? evalAdapter.jobCancel(job)
                    }
                }
            }
        } onCancel: {
            cancellation.cancel()
        }
    }
}

private final class EcritumJobCancellationBox: @unchecked Sendable {
    private let lock = NSLock()
    private weak var adapter: EcritumEvalABI?
    private var job: ecritum_job_t = 0
    private var didCancel = false

    func set(adapter: EcritumEvalABI, job: ecritum_job_t) {
        lock.lock()
        self.adapter = adapter
        self.job = job
        lock.unlock()
    }

    func clear() {
        lock.lock()
        adapter = nil
        job = 0
        lock.unlock()
    }

    func cancel() {
        lock.lock()
        guard !didCancel, let adapter, job != 0 else {
            lock.unlock()
            return
        }
        didCancel = true
        let cancelledJob = job
        lock.unlock()
        try? adapter.jobCancel(cancelledJob)
    }
}

final class EcritumCLifecycleABI: EcritumLifecycleABI, EcritumEvalABI, EcritumCallABI {
    static let shared = EcritumCLifecycleABI()

    private init() {}

    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let configData = try configuration.abiConfigData()
        var runtime: ecritum_runtime_t = 0
        var error: ecritum_error_t = 0
        let status = configData.withUnsafeBytes { buffer in
            ecritum_runtime_create(bytes(buffer), &runtime, &error)
        }
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return runtime
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_runtime_destroy(&handle, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
        #else
        handle = 0
        #endif
    }

    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let configData = try configuration.abiConfigData()
        var context: ecritum_context_t = 0
        var error: ecritum_error_t = 0
        let status = configData.withUnsafeBytes { buffer in
            ecritum_context_create(runtime, bytes(buffer), &context, &error)
        }
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return context
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func contextDestroy(_ handle: inout ecritum_context_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_context_destroy(&handle, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
        #else
        handle = 0
        #endif
    }

    func namespaceCreate(runtime: ecritum_runtime_t, name: EcritumNamespace.Name) throws -> ecritum_namespace_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var namespace: ecritum_namespace_t = 0
        var error: ecritum_error_t = 0
        var rawName = name.rawValue
        let status = rawName.withUTF8 { buffer in
            ecritum_namespace_create(runtime, stringView(buffer), &namespace, &error)
        }
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
        return namespace
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func namespaceDestroy(_ handle: inout ecritum_namespace_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_namespace_destroy(&handle, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
        #else
        handle = 0
        #endif
    }

    func registerFunction(namespace: ecritum_namespace_t, name: EcritumFunctionName, callback: @escaping EcritumHostFunction) throws -> ecritum_function_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var function: ecritum_function_t = 0
        var error: ecritum_error_t = 0
        let box = Unmanaged.passRetained(EcritumHostFunctionBox(callback))
        var rawName = name.rawValue
        let status = rawName.withUTF8 { buffer in
            ecritum_namespace_register_function(
                namespace,
                stringView(buffer),
                EcritumCLifecycleABI.hostFunctionCallback,
                box.toOpaque(),
                EcritumCLifecycleABI.destroyHostFunctionBox,
                &function,
                &error
            )
        }
        guard status == ECRITUM_OK else {
            box.release()
            throw copyError(status: status, error: error)
        }
        return function
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func functionDestroy(_ handle: inout ecritum_function_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_function_destroy(&handle, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
        #else
        handle = 0
        #endif
    }

    func evalStart(context: ecritum_context_t, script: EcritumScript) throws -> ecritum_job_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let sourceData = Data(script.source.utf8)
        let optionsData = try script.options.abiOptionsData()
        var language = script.language.rawValue
        var sourceName = script.sourceName ?? ""
        var job: ecritum_job_t = 0
        var error: ecritum_error_t = 0
        let status = sourceData.withUnsafeBytes { sourceBuffer in
            optionsData.withUnsafeBytes { optionsBuffer in
                language.withUTF8 { languageBuffer in
                    sourceName.withUTF8 { sourceNameBuffer in
                        ecritum_eval_start(
                            context,
                            stringView(languageBuffer),
                            bytes(sourceBuffer),
                            stringView(sourceNameBuffer),
                            bytes(optionsBuffer),
                            &job,
                            &error
                        )
                    }
                }
            }
        }
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return job
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func jobWait(_ job: ecritum_job_t, timeoutNanoseconds: UInt64) throws -> EcritumJobState {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var state: Int32 = 0
        var error: ecritum_error_t = 0
        let status = ecritum_job_wait(job, timeoutNanoseconds, &state, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return EcritumJobState(rawValue: state) ?? .poisoned
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func jobCancel(_ job: ecritum_job_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_job_cancel(job, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        #else
        _ = job
        #endif
    }

    func jobResult(_ job: ecritum_job_t) throws -> ecritum_value_t {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var value: ecritum_value_t = 0
        var error: ecritum_error_t = 0
        let status = ecritum_job_result(job, &value, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return value
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func jobDestroy(_ job: inout ecritum_job_t) throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var error: ecritum_error_t = 0
        let status = ecritum_job_destroy(&job, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        #else
        job = 0
        #endif
    }

    func copyValue(_ value: ecritum_value_t) throws -> EcritumValue {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        return try copyNativeValue(value)
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func valueDestroy(_ value: inout ecritum_value_t) {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        _ = ecritum_value_destroy(&value)
        #else
        value = 0
        #endif
    }

    func callArgumentCount(_ call: ecritum_call_t) throws -> Int {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var count = 0
        var error: ecritum_error_t = 0
        let status = ecritum_call_argument_count(call, &count, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return count
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    func callArgument(_ call: ecritum_call_t, index: Int) throws -> ecritum_value_t {
        guard index >= 0 else {
            throw EcritumError.invalidArgument(.lifecycle(.invalidArgument, operation: "call_argument"))
        }
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var value: ecritum_value_t = 0
        var error: ecritum_error_t = 0
        let status = ecritum_call_argument(call, index, &value, &error)
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return value
        #else
        throw EcritumError.runtimeArtifactMissing
        #endif
    }

    #if ECRITUM_HAS_RUNTIME_ARTIFACT
    private func bytes(_ buffer: UnsafeRawBufferPointer) -> ecritum_bytes_t {
        ecritum_bytes_t(
            data: buffer.bindMemory(to: UInt8.self).baseAddress,
            len: buffer.count
        )
    }

    private func stringView(_ buffer: UnsafeBufferPointer<UInt8>) -> ecritum_string_view_t {
        ecritum_string_view_t(
            data: buffer.baseAddress.map { UnsafeRawPointer($0).assumingMemoryBound(to: CChar.self) },
            len: buffer.count
        )
    }

    private func copyError(status: Int32, error: ecritum_error_t) -> EcritumError {
        var ownedError = error
        let details = ownedError == 0 ? nil : copyDetails(error: ownedError, fallbackStatus: status)
        if ownedError != 0 {
            _ = ecritum_error_destroy(&ownedError)
        }
        return EcritumError.from(status: status, details: details)
    }

    private func copyDetails(error: ecritum_error_t, fallbackStatus: Int32) -> EcritumErrorDetails {
        var statusValue = fallbackStatus
        _ = ecritum_error_status(error, &statusValue)

        let status = EcritumStatus(rawValue: statusValue)
        let category = copyView(error, using: ecritum_error_category).flatMap(EcritumErrorCategory.init(rawValue:))
            ?? status.map { EcritumErrorCategory(status: $0) }
            ?? .unknown
        let message = copyView(error, using: ecritum_error_message) ?? "Ecritum operation failed"
        let operation = copyView(error, using: ecritum_error_operation)
        let language = copyView(error, using: ecritum_error_language)
        let sourceName = copyView(error, using: ecritum_error_source_name)

        return EcritumErrorDetails(
            status: status,
            category: category,
            message: message,
            operation: operation,
            language: language,
            sourceName: sourceName
        )
    }

    private func copyView(
        _ error: ecritum_error_t,
        using accessor: (ecritum_error_t, UnsafeMutablePointer<ecritum_string_view_t>?) -> Int32
    ) -> String? {
        var view = ecritum_string_view_t(data: nil, len: 0)
        guard accessor(error, &view) == ECRITUM_OK, let data = view.data, view.len > 0 else {
            return nil
        }

        let bytes = UnsafeRawPointer(data).assumingMemoryBound(to: UInt8.self)
        let buffer = UnsafeBufferPointer(start: bytes, count: Int(view.len))
        return String(decoding: buffer, as: UTF8.self)
    }

    private func copyNativeValue(_ value: ecritum_value_t) throws -> EcritumValue {
        var kind: Int32 = 0
        let kindStatus = ecritum_value_kind(value, &kind)
        guard kindStatus == ECRITUM_OK else {
            throw copyError(status: kindStatus, error: 0)
        }

        switch kind {
        case ECRITUM_VALUE_KIND_NULL:
            return .null
        case ECRITUM_VALUE_KIND_BOOL:
            var raw = Int32(0)
            let status = ecritum_value_get_bool(value, &raw)
            guard status == ECRITUM_OK else { throw copyError(status: status, error: 0) }
            return .bool(raw != 0)
        case ECRITUM_VALUE_KIND_INT:
            var raw = Int64(0)
            let status = ecritum_value_get_int(value, &raw)
            guard status == ECRITUM_OK else { throw copyError(status: status, error: 0) }
            return .int(raw)
        case ECRITUM_VALUE_KIND_DOUBLE:
            var raw = 0.0
            let status = ecritum_value_get_double(value, &raw)
            guard status == ECRITUM_OK else { throw copyError(status: status, error: 0) }
            return .double(raw)
        case ECRITUM_VALUE_KIND_STRING:
            var view = ecritum_string_view_t(data: nil, len: 0)
            let status = ecritum_value_get_string(value, &view)
            guard status == ECRITUM_OK else { throw copyError(status: status, error: 0) }
            return .string(copyString(view))
        case ECRITUM_VALUE_KIND_DATA:
            var view = ecritum_bytes_t(data: nil, len: 0)
            let status = ecritum_value_get_data(value, &view)
            guard status == ECRITUM_OK else { throw copyError(status: status, error: 0) }
            return .data(copyData(view))
        case ECRITUM_VALUE_KIND_ARRAY:
            var count = 0
            let countStatus = ecritum_value_count(value, &count)
            guard countStatus == ECRITUM_OK else { throw copyError(status: countStatus, error: 0) }
            var values: [EcritumValue] = []
            values.reserveCapacity(count)
            for index in 0..<count {
                var child: ecritum_value_t = 0
                var error: ecritum_error_t = 0
                let status = ecritum_value_array_get(value, index, &child, &error)
                guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
                defer { var owned = child; _ = ecritum_value_destroy(&owned) }
                values.append(try copyNativeValue(child))
            }
            return .array(values)
        case ECRITUM_VALUE_KIND_OBJECT:
            var count = 0
            let countStatus = ecritum_value_count(value, &count)
            guard countStatus == ECRITUM_OK else { throw copyError(status: countStatus, error: 0) }
            var object: [String: EcritumValue] = [:]
            object.reserveCapacity(count)
            for index in 0..<count {
                var key = ecritum_string_view_t(data: nil, len: 0)
                var child: ecritum_value_t = 0
                var error: ecritum_error_t = 0
                let status = ecritum_value_object_entry(value, index, &key, &child, &error)
                guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
                defer { var owned = child; _ = ecritum_value_destroy(&owned) }
                object[copyString(key)] = try copyNativeValue(child)
            }
            return .object(object)
        default:
            throw EcritumError.internalFailure(.lifecycle(.internalFailure, operation: "value_copy"))
        }
    }

    private func copyString(_ view: ecritum_string_view_t) -> String {
        guard let data = view.data, view.len > 0 else { return "" }
        let bytes = UnsafeRawPointer(data).assumingMemoryBound(to: UInt8.self)
        let buffer = UnsafeBufferPointer(start: bytes, count: Int(view.len))
        return String(decoding: buffer, as: UTF8.self)
    }

    private func copyData(_ view: ecritum_bytes_t) -> Data {
        guard let data = view.data, view.len > 0 else { return Data() }
        return Data(bytes: data, count: Int(view.len))
    }

    func makeNativeValue(_ value: EcritumValue) throws -> ecritum_value_t {
        var result: ecritum_value_t = 0
        var error: ecritum_error_t = 0
        let status: Int32
        switch value {
        case .null:
            status = ecritum_value_make_null(&result, &error)
        case let .bool(raw):
            status = ecritum_value_make_bool(raw ? 1 : 0, &result, &error)
        case let .int(raw):
            status = ecritum_value_make_int(raw, &result, &error)
        case let .double(raw):
            status = ecritum_value_make_double(raw, &result, &error)
        case let .string(raw):
            var mutable = raw
            status = mutable.withUTF8 { buffer in
                ecritum_value_make_string(stringView(buffer), &result, &error)
            }
        case let .data(raw):
            status = raw.withUnsafeBytes { buffer in
                ecritum_value_make_data(bytes(buffer), &result, &error)
            }
        case let .array(values):
            var handles = try makeNativeValues(values)
            defer {
                for index in handles.indices {
                    _ = ecritum_value_destroy(&handles[index])
                }
            }
            status = handles.withUnsafeBufferPointer { buffer in
                ecritum_value_make_array(buffer.baseAddress, buffer.count, &result, &error)
            }
        case let .object(values):
            let sorted = values.sorted { $0.key < $1.key }
            var handles = try makeNativeValues(sorted.map(\.value))
            defer {
                for index in handles.indices {
                    _ = ecritum_value_destroy(&handles[index])
                }
            }
            let keyPointers = try makeKeyPointers(sorted.map(\.key))
            defer {
                for pointer in keyPointers {
                    free(pointer)
                }
            }
            var entries: [ecritum_object_entry_t] = []
            entries.reserveCapacity(sorted.count)
            for index in sorted.indices {
                let pointer = keyPointers[index]
                entries.append(ecritum_object_entry_t(
                    key: ecritum_string_view_t(data: pointer, len: sorted[index].key.utf8.count),
                    value: handles[index]
                ))
            }
            status = entries.withUnsafeBufferPointer { buffer in
                ecritum_value_make_object(buffer.baseAddress, buffer.count, &result, &error)
            }
        }
        guard status == ECRITUM_OK else { throw copyError(status: status, error: error) }
        return result
    }

    private func makeNativeValues(_ values: [EcritumValue]) throws -> [ecritum_value_t] {
        var handles: [ecritum_value_t] = []
        handles.reserveCapacity(values.count)
        do {
            for value in values {
                handles.append(try makeNativeValue(value))
            }
            return handles
        } catch {
            for index in handles.indices {
                _ = ecritum_value_destroy(&handles[index])
            }
            throw error
        }
    }

    private func makeKeyPointer(_ key: String) throws -> UnsafeMutablePointer<CChar> {
        let keyBytes = Array(key.utf8)
        guard let rawPointer = malloc(max(keyBytes.count, 1)) else {
            throw EcritumError.outOfMemory(.lifecycle(.outOfMemory, operation: "value_make_object"))
        }
        if !keyBytes.isEmpty {
            keyBytes.withUnsafeBufferPointer { buffer in
                rawPointer.copyMemory(from: buffer.baseAddress!, byteCount: keyBytes.count)
            }
        }
        return rawPointer.assumingMemoryBound(to: CChar.self)
    }

    private func makeKeyPointers(_ keys: [String]) throws -> [UnsafeMutablePointer<CChar>] {
        var pointers: [UnsafeMutablePointer<CChar>] = []
        pointers.reserveCapacity(keys.count)
        do {
            for key in keys {
                pointers.append(try makeKeyPointer(key))
            }
            return pointers
        } catch {
            for pointer in pointers {
                free(pointer)
            }
            throw error
        }
    }
    #endif
}

private final class EcritumHostFunctionBox {
    let callback: EcritumHostFunction

    init(_ callback: @escaping EcritumHostFunction) {
        self.callback = callback
    }
}

private extension EcritumCLifecycleABI {
    static let hostFunctionCallback: ecritum_host_fn_t = { call, outResult, outError, userData in
        if outResult != nil {
            outResult?.pointee = 0
        }
        if outError != nil {
            outError?.pointee = 0
        }
        guard let userData else {
            return ECRITUM_ERROR_CALLBACK
        }

        let box = Unmanaged<EcritumHostFunctionBox>.fromOpaque(userData).takeUnretainedValue()
        do {
            let value = try box.callback(EcritumCall(handle: call, adapter: EcritumCLifecycleABI.shared))
            #if ECRITUM_HAS_RUNTIME_ARTIFACT
            if let outResult {
                outResult.pointee = try EcritumCLifecycleABI.shared.makeNativeValue(value)
            }
            #else
            _ = value
            #endif
            return ECRITUM_OK
        } catch {
            return ECRITUM_ERROR_CALLBACK
        }
    }

    static let destroyHostFunctionBox: ecritum_user_data_destroy_fn_t = { userData in
        guard let userData else {
            return
        }
        Unmanaged<EcritumHostFunctionBox>.fromOpaque(userData).release()
    }
}

extension EcritumErrorDetails {
    static func lifecycle(_ status: EcritumStatus, operation: String) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "Ecritum lifecycle operation failed",
            operation: operation
        )
    }
}
