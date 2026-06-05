import Foundation

private enum EcritumErrorRedaction {
    static func message(_ message: String) -> String {
        let trimmed = message.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            return "Ecritum operation failed"
        }

        guard !containsUnsafeMarker(trimmed) else {
            return "Ecritum operation failed"
        }

        return trimmed
    }

    static func field(_ value: String?) -> String? {
        guard let value else {
            return nil
        }

        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty, !containsUnsafeMarker(trimmed) else {
            return nil
        }

        return trimmed
    }

    private static func containsUnsafeMarker(_ value: String) -> Bool {
        let unsafeMarkers = [
            "java.lang",
            "org.graalvm",
            "com.oracle.truffle",
            "throwable",
            "stacktrace",
            "/users/",
            "/private/var/",
            "/var/folders/",
            "secret",
            "token=",
            "password=",
            "processbuilder",
        ]
        let lowercased = value.lowercased()
        return unsafeMarkers.contains(where: lowercased.contains)
    }
}

/// Stable Ecritum status codes shared by the C ABI, native runtime, and Swift wrapper.
public enum EcritumStatus: Int32, CaseIterable, Sendable {
    case ok = 0
    case invalidArgument = 1
    case bufferTooSmall = 2
    case runtimeUnavailable = 3
    case invalidHandle = 4
    case outOfMemory = 5
    case invalidUTF8 = 6
    case inputTooLarge = 7
    case invalidConfig = 8
    case unsupportedConfigVersion = 9
    case contextsAlive = 10
    case closed = 11
    case busy = 12
    case reentrantCall = 13
    case permissionDenied = 14
    case timeout = 15
    case cancelled = 16
    case script = 17
    case callback = 18
    case teardownFailed = 19
    case internalFailure = 20
    case alreadyExists = 21
}

/// Stable machine-readable category for an Ecritum error.
public enum EcritumErrorCategory: String, Sendable {
    case runtimeArtifactMissing = "runtime_artifact_missing"
    case invalidArgument = "invalid_argument"
    case invalidHandle = "invalid_handle"
    case bufferTooSmall = "buffer_too_small"
    case outOfMemory = "out_of_memory"
    case invalidUTF8 = "invalid_utf8"
    case inputTooLarge = "input_too_large"
    case invalidConfig = "invalid_config"
    case unsupportedConfigVersion = "unsupported_config_version"
    case contextsAlive = "contexts_alive"
    case closed
    case busy
    case reentrantCall = "reentrant_call"
    case permissionDenied = "permission_denied"
    case timeout
    case cancelled
    case script
    case syntax
    case runtime
    case permission
    case callback
    case runtimeUnavailable = "runtime_unavailable"
    case teardownFailed = "teardown_failed"
    case internalFailure = "internal"
    case alreadyExists = "already_exists"
    case unknown

    public init(status: EcritumStatus) {
        switch status {
        case .ok:
            self = .unknown
        case .invalidArgument:
            self = .invalidArgument
        case .invalidHandle:
            self = .invalidHandle
        case .bufferTooSmall:
            self = .bufferTooSmall
        case .outOfMemory:
            self = .outOfMemory
        case .invalidUTF8:
            self = .invalidUTF8
        case .inputTooLarge:
            self = .inputTooLarge
        case .invalidConfig:
            self = .invalidConfig
        case .unsupportedConfigVersion:
            self = .unsupportedConfigVersion
        case .contextsAlive:
            self = .contextsAlive
        case .closed:
            self = .closed
        case .busy:
            self = .busy
        case .reentrantCall:
            self = .reentrantCall
        case .permissionDenied:
            self = .permissionDenied
        case .timeout:
            self = .timeout
        case .cancelled:
            self = .cancelled
        case .script:
            self = .script
        case .callback:
            self = .callback
        case .runtimeUnavailable:
            self = .runtimeUnavailable
        case .teardownFailed:
            self = .teardownFailed
        case .internalFailure:
            self = .internalFailure
        case .alreadyExists:
            self = .alreadyExists
        }
    }
}

/// One user-facing stack frame associated with an Ecritum diagnostic.
public struct EcritumStackFrame: Equatable, Sendable {
    public private(set) var function: String?
    public private(set) var sourceName: String?
    public private(set) var line: Int?
    public private(set) var column: Int?

    public init(
        function: String? = nil,
        sourceName: String? = nil,
        line: Int? = nil,
        column: Int? = nil
    ) {
        self.function = EcritumErrorRedaction.field(function)
        self.sourceName = EcritumErrorRedaction.field(sourceName)
        self.line = line
        self.column = column
    }
}

/// Safe diagnostics copied out of an Ecritum error object.
public struct EcritumErrorDetails: Equatable, Sendable {
    public private(set) var status: EcritumStatus?
    public private(set) var category: EcritumErrorCategory
    public private(set) var message: String
    public private(set) var operation: String?
    public private(set) var language: String?
    public private(set) var sourceName: String?
    public private(set) var line: Int?
    public private(set) var column: Int?
    public private(set) var stack: [EcritumStackFrame]

    public init(
        status: EcritumStatus?,
        category: EcritumErrorCategory,
        message: String,
        operation: String? = nil,
        language: String? = nil,
        sourceName: String? = nil,
        line: Int? = nil,
        column: Int? = nil,
        stack: [EcritumStackFrame] = []
    ) {
        self.status = status
        self.category = category
        self.message = EcritumErrorRedaction.message(message)
        self.operation = EcritumErrorRedaction.field(operation)
        self.language = EcritumErrorRedaction.field(language)
        self.sourceName = EcritumErrorRedaction.field(sourceName)
        self.line = line
        self.column = column
        self.stack = stack
    }

    func normalized(for status: EcritumStatus) -> EcritumErrorDetails {
        var details = self
        details.status = status
        if details.category == .unknown {
            details.category = EcritumErrorCategory(status: status)
        }
        return details
    }
}

/// Errors surfaced by the Ecritum Swift wrapper.
public enum EcritumError: Error, Equatable, Sendable {
    /// No local or release runtime artifact was resolved by Package.swift.
    case runtimeArtifactMissing
    case invalidArgument(EcritumErrorDetails)
    case invalidHandle(EcritumErrorDetails)
    case bufferTooSmall(EcritumErrorDetails)
    case outOfMemory(EcritumErrorDetails)
    case invalidUTF8(EcritumErrorDetails)
    case inputTooLarge(EcritumErrorDetails)
    case invalidConfig(EcritumErrorDetails)
    case unsupportedConfigVersion(EcritumErrorDetails)
    case contextsAlive(EcritumErrorDetails)
    case closed(EcritumErrorDetails)
    case busy(EcritumErrorDetails)
    case reentrantCall(EcritumErrorDetails)
    case permissionDenied(EcritumErrorDetails)
    case timeout(EcritumErrorDetails)
    case cancelled(EcritumErrorDetails)
    case script(EcritumErrorDetails)
    case callback(EcritumErrorDetails)
    case runtimeUnavailable(EcritumErrorDetails)
    case teardownFailed(EcritumErrorDetails)
    case internalFailure(EcritumErrorDetails)
    case alreadyExists(EcritumErrorDetails)
    case unknownStatus(rawStatus: Int32, details: EcritumErrorDetails?)

    static func from(
        status rawStatus: Int32,
        details: EcritumErrorDetails? = nil
    ) -> EcritumError {
        guard let status = EcritumStatus(rawValue: rawStatus), status != .ok else {
            return .unknownStatus(rawStatus: rawStatus, details: details)
        }

        let details = (details ?? EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: Self.defaultMessage(for: status)
        )).normalized(for: status)

        switch status {
        case .ok:
            return .unknownStatus(rawStatus: rawStatus, details: details)
        case .invalidArgument:
            return .invalidArgument(details)
        case .invalidHandle:
            return .invalidHandle(details)
        case .bufferTooSmall:
            return .bufferTooSmall(details)
        case .outOfMemory:
            return .outOfMemory(details)
        case .invalidUTF8:
            return .invalidUTF8(details)
        case .inputTooLarge:
            return .inputTooLarge(details)
        case .invalidConfig:
            return .invalidConfig(details)
        case .unsupportedConfigVersion:
            return .unsupportedConfigVersion(details)
        case .contextsAlive:
            return .contextsAlive(details)
        case .closed:
            return .closed(details)
        case .busy:
            return .busy(details)
        case .reentrantCall:
            return .reentrantCall(details)
        case .permissionDenied:
            return .permissionDenied(details)
        case .timeout:
            return .timeout(details)
        case .cancelled:
            return .cancelled(details)
        case .script:
            return .script(details)
        case .callback:
            return .callback(details)
        case .runtimeUnavailable:
            return .runtimeUnavailable(details)
        case .teardownFailed:
            return .teardownFailed(details)
        case .internalFailure:
            return .internalFailure(details)
        case .alreadyExists:
            return .alreadyExists(details)
        }
    }

    public var status: EcritumStatus? {
        switch self {
        case .runtimeArtifactMissing:
            return nil
        case .invalidArgument:
            return .invalidArgument
        case .invalidHandle:
            return .invalidHandle
        case .bufferTooSmall:
            return .bufferTooSmall
        case .outOfMemory:
            return .outOfMemory
        case .invalidUTF8:
            return .invalidUTF8
        case .inputTooLarge:
            return .inputTooLarge
        case .invalidConfig:
            return .invalidConfig
        case .unsupportedConfigVersion:
            return .unsupportedConfigVersion
        case .contextsAlive:
            return .contextsAlive
        case .closed:
            return .closed
        case .busy:
            return .busy
        case .reentrantCall:
            return .reentrantCall
        case .permissionDenied:
            return .permissionDenied
        case .timeout:
            return .timeout
        case .cancelled:
            return .cancelled
        case .script:
            return .script
        case .callback:
            return .callback
        case .runtimeUnavailable:
            return .runtimeUnavailable
        case .teardownFailed:
            return .teardownFailed
        case .internalFailure:
            return .internalFailure
        case .alreadyExists:
            return .alreadyExists
        case let .unknownStatus(_, details):
            return details?.status
        }
    }

    public var category: EcritumErrorCategory {
        switch self {
        case .runtimeArtifactMissing:
            return .runtimeArtifactMissing
        case let .invalidArgument(details),
             let .invalidHandle(details),
             let .bufferTooSmall(details),
             let .outOfMemory(details),
             let .invalidUTF8(details),
             let .inputTooLarge(details),
             let .invalidConfig(details),
             let .unsupportedConfigVersion(details),
             let .contextsAlive(details),
             let .closed(details),
             let .busy(details),
             let .reentrantCall(details),
             let .permissionDenied(details),
             let .timeout(details),
             let .cancelled(details),
             let .script(details),
             let .callback(details),
             let .runtimeUnavailable(details),
             let .teardownFailed(details),
             let .internalFailure(details),
             let .alreadyExists(details):
            return details.category
        case let .unknownStatus(_, details):
            return details?.category ?? .unknown
        }
    }

    public var details: EcritumErrorDetails? {
        switch self {
        case .runtimeArtifactMissing:
            return nil
        case let .invalidArgument(details),
             let .invalidHandle(details),
             let .bufferTooSmall(details),
             let .outOfMemory(details),
             let .invalidUTF8(details),
             let .inputTooLarge(details),
             let .invalidConfig(details),
             let .unsupportedConfigVersion(details),
             let .contextsAlive(details),
             let .closed(details),
             let .busy(details),
             let .reentrantCall(details),
             let .permissionDenied(details),
             let .timeout(details),
             let .cancelled(details),
             let .script(details),
             let .callback(details),
             let .runtimeUnavailable(details),
             let .teardownFailed(details),
             let .internalFailure(details),
             let .alreadyExists(details):
            return details
        case let .unknownStatus(_, details):
            return details
        }
    }

    private static func defaultMessage(for status: EcritumStatus) -> String {
        switch status {
        case .ok:
            return "Ecritum operation succeeded"
        case .invalidArgument:
            return "Invalid argument"
        case .invalidHandle:
            return "Invalid handle"
        case .bufferTooSmall:
            return "Output buffer is too small"
        case .outOfMemory:
            return "Ecritum ran out of memory"
        case .invalidUTF8:
            return "Input is not valid UTF-8"
        case .inputTooLarge:
            return "Input is too large"
        case .invalidConfig:
            return "Invalid Ecritum configuration"
        case .unsupportedConfigVersion:
            return "Unsupported Ecritum configuration version"
        case .contextsAlive:
            return "Contexts are still alive"
        case .closed:
            return "Ecritum object is closed"
        case .busy:
            return "Ecritum object is busy"
        case .reentrantCall:
            return "Reentrant Ecritum call is not allowed"
        case .permissionDenied:
            return "Permission denied"
        case .timeout:
            return "Ecritum operation timed out"
        case .cancelled:
            return "Ecritum operation was cancelled"
        case .script:
            return "Script failed"
        case .callback:
            return "Host callback failed"
        case .runtimeUnavailable:
            return "Ecritum runtime is unavailable"
        case .teardownFailed:
            return "Ecritum teardown failed"
        case .internalFailure:
            return "Ecritum operation failed"
        case .alreadyExists:
            return "Ecritum object already exists"
        }
    }
}

extension EcritumError: LocalizedError {
    public var errorDescription: String? {
        switch self {
        case .runtimeArtifactMissing:
            return "Ecritum runtime artifact is not available"
        case let .unknownStatus(_, details):
            return details?.message ?? "Ecritum operation failed"
        default:
            return details?.message ?? "Ecritum operation failed"
        }
    }
}
