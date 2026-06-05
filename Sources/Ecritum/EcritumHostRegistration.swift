import CEcritum
import Foundation

public typealias EcritumHostFunction = @Sendable (EcritumCall) throws -> EcritumValue

private enum EcritumNameValidation {
    static let maxBytes = 255
    private static let reservedPrefixes = ["ecritum", "java", "javax", "sun", "graal", "truffle"]

    static func namespace(_ rawValue: String) throws -> String {
        try validateLength(rawValue, operation: "namespace_create")
        let parts = rawValue.split(separator: ".", omittingEmptySubsequences: false)
        guard !parts.isEmpty else {
            throw invalidName(operation: "namespace_create")
        }
        for part in parts {
            guard isValidIdentifier(part.utf8) else {
                throw invalidName(operation: "namespace_create")
            }
        }
        guard let first = parts.first, !isReserved(String(first)) else {
            throw invalidName(operation: "namespace_create")
        }
        return rawValue
    }

    static func function(_ rawValue: String) throws -> String {
        try validateLength(rawValue, operation: "function_register")
        guard isValidIdentifier(rawValue.utf8), !isReserved(rawValue) else {
            throw invalidName(operation: "function_register")
        }
        return rawValue
    }

    private static func validateLength(_ rawValue: String, operation: String) throws {
        guard rawValue.utf8.count <= maxBytes else {
            throw EcritumError.inputTooLarge(EcritumErrorDetails(
                status: .inputTooLarge,
                category: .inputTooLarge,
                message: "Ecritum name is too large",
                operation: operation
            ))
        }
    }

    private static func isValidIdentifier<T: Collection>(_ bytes: T) -> Bool where T.Element == UInt8 {
        guard let first = bytes.first, isASCIIAlpha(first) else {
            return false
        }
        return bytes.dropFirst().allSatisfy { isASCIIAlpha($0) || isASCIIDigit($0) || $0 == UInt8(ascii: "_") }
    }

    private static func isASCIIAlpha(_ byte: UInt8) -> Bool {
        (UInt8(ascii: "A")...UInt8(ascii: "Z")).contains(byte) || (UInt8(ascii: "a")...UInt8(ascii: "z")).contains(byte)
    }

    private static func isASCIIDigit(_ byte: UInt8) -> Bool {
        (UInt8(ascii: "0")...UInt8(ascii: "9")).contains(byte)
    }

    private static func isReserved(_ value: String) -> Bool {
        let lowercased = value.lowercased()
        return reservedPrefixes.contains { lowercased.hasPrefix($0) }
    }

    private static func invalidName(operation: String) -> EcritumError {
        EcritumError.invalidArgument(EcritumErrorDetails(
            status: .invalidArgument,
            category: .invalidArgument,
            message: "Invalid Ecritum name",
            operation: operation
        ))
    }
}

public final class EcritumNamespace {
    public struct Name: Hashable, Sendable {
        public let rawValue: String

        public init(_ rawValue: String) throws {
            self.rawValue = try EcritumNameValidation.namespace(rawValue)
        }
    }

    private let parent: EcritumRuntime
    private let adapter: EcritumLifecycleABI
    private var handle: ecritum_namespace_t
    private var functionHandles: [ecritum_function_t] = []

    init(parent: EcritumRuntime, handle: ecritum_namespace_t, adapter: EcritumLifecycleABI) {
        self.parent = parent
        self.handle = handle
        self.adapter = adapter
    }

    deinit {
        try? close()
    }

    public func register(
        _ name: EcritumFunctionName,
        _ function: @escaping EcritumHostFunction
    ) throws {
        guard parent.isOpen, handle != 0 else {
            throw EcritumError.closed(EcritumErrorDetails(
                status: .closed,
                category: .closed,
                message: "Ecritum namespace is closed",
                operation: "function_register"
            ))
        }

        let functionHandle = try adapter.registerFunction(namespace: handle, name: name, callback: function)
        functionHandles.append(functionHandle)
    }

    public func close() throws {
        guard handle != 0 else {
            return
        }
        guard parent.isOpen else {
            handle = 0
            functionHandles.removeAll()
            return
        }

        try adapter.namespaceDestroy(&handle)
        functionHandles.removeAll()
    }
}

public struct EcritumFunctionName: Hashable, Sendable {
    public let rawValue: String

    public init(_ rawValue: String) throws {
        self.rawValue = try EcritumNameValidation.function(rawValue)
    }
}

public final class EcritumCall {
    private let handle: ecritum_call_t
    private weak var adapter: EcritumCallABI?
    private let fixtureArguments: [EcritumValue]?

    init(handle: ecritum_call_t = 0, adapter: EcritumCallABI? = nil, fixtureArguments: [EcritumValue]? = nil) {
        self.handle = handle
        self.adapter = adapter
        self.fixtureArguments = fixtureArguments
    }

    public func argumentCount() throws -> Int {
        if let fixtureArguments {
            return fixtureArguments.count
        }
        guard let adapter else {
            throw EcritumError.invalidHandle(.lifecycle(.invalidHandle, operation: "call_argument_count"))
        }
        return try adapter.callArgumentCount(handle)
    }

    public func value(at index: Int) throws -> EcritumValue {
        guard index >= 0 else {
            throw EcritumError.invalidArgument(.lifecycle(.invalidArgument, operation: "call_argument"))
        }
        if let fixtureArguments {
            guard index < fixtureArguments.count else {
                throw EcritumError.invalidArgument(.lifecycle(.invalidArgument, operation: "call_argument"))
            }
            return fixtureArguments[index]
        }
        guard let adapter else {
            throw EcritumError.invalidHandle(.lifecycle(.invalidHandle, operation: "call_argument"))
        }
        var nativeValue = try adapter.callArgument(handle, index: index)
        defer { adapter.valueDestroy(&nativeValue) }
        return try adapter.copyValue(nativeValue)
    }
}
