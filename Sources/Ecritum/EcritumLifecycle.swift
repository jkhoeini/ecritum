import CEcritum
import Foundation

protocol EcritumLifecycleABI: AnyObject {
    func runtimeCreate(configuration: EcritumRuntime.Configuration) throws -> ecritum_runtime_t
    func runtimeDestroy(_ handle: inout ecritum_runtime_t) throws
    func contextCreate(runtime: ecritum_runtime_t, configuration: EcritumContext.Configuration) throws -> ecritum_context_t
    func contextDestroy(_ handle: inout ecritum_context_t) throws
}

/// Owns a packaged Ecritum runtime instance.
public final class EcritumRuntime {
    public struct Configuration: Equatable, Sendable {
        public static let `default` = Configuration()

        public init() {}
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

    public func context(_ configuration: EcritumContext.Configuration = .default) throws -> EcritumContext {
        guard handle != 0 else {
            throw EcritumError.closed(.lifecycle(.closed, operation: "context_create"))
        }

        let contextHandle = try adapter.contextCreate(runtime: handle, configuration: configuration)
        return EcritumContext(parent: self, handle: contextHandle, adapter: adapter)
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

        public init() {}
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
        var runtime: ecritum_runtime_t = 0
        var error: ecritum_error_t = 0
        let status = ecritum_runtime_create(emptyBytes(), &runtime, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
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
        var context: ecritum_context_t = 0
        var error: ecritum_error_t = 0
        let status = ecritum_context_create(runtime, emptyBytes(), &context, &error)
        guard status == ECRITUM_OK else {
            throw copyError(status: status, error: error)
        }
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

    #if ECRITUM_HAS_RUNTIME_ARTIFACT
    private func emptyBytes() -> ecritum_bytes_t {
        ecritum_bytes_t(data: nil, len: 0)
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
