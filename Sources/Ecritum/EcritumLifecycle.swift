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
}

private final class EcritumCLifecycleABI: EcritumLifecycleABI {
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

        return EcritumErrorDetails(
            status: status,
            category: category,
            message: message,
            operation: operation
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
            _ = try box.callback(EcritumCall(handle: call))
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

private extension EcritumErrorDetails {
    static func lifecycle(_ status: EcritumStatus, operation: String) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "Ecritum lifecycle operation failed",
            operation: operation
        )
    }
}
