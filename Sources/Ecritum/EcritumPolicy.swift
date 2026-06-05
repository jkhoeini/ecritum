import Foundation

public struct EcritumConfigurationSchemaVersion: RawRepresentable, Equatable, Sendable {
    public static let v1 = EcritumConfigurationSchemaVersion(rawValue: 1)

    public let rawValue: UInt32

    public init(rawValue: UInt32) {
        self.rawValue = rawValue
    }
}

public struct EcritumLanguage: RawRepresentable, Hashable, Sendable {
    public static let clojure = EcritumLanguage(rawValue: "clojure")
    public static let javascript = EcritumLanguage(rawValue: "javascript")
    public static let lua = EcritumLanguage(rawValue: "lua")
    public static let python = EcritumLanguage(rawValue: "python")
    public static let ruby = EcritumLanguage(rawValue: "ruby")

    public let rawValue: String

    public init(rawValue: String) {
        self.rawValue = rawValue
    }
}

public enum EcritumDiagnosticsPolicy: Equatable, Sendable {
    case redacted
    case raw
}

public struct EcritumResourceLimits: Equatable, Sendable {
    public struct Narrowing: Equatable, Sendable {
        public var executionTimeoutNanos: UInt64?
        public var maxInputBytes: UInt64?
        public var maxOutputBytes: UInt64?
        public var maxStackDepth: UInt64?
        public var maxHeapBytes: UInt64?
        public var maxCallbackQueueLength: UInt64?
        public var callbackTimeoutNanos: UInt64?

        public init(
            executionTimeoutNanos: UInt64? = nil,
            maxInputBytes: UInt64? = nil,
            maxOutputBytes: UInt64? = nil,
            maxStackDepth: UInt64? = nil,
            maxHeapBytes: UInt64? = nil,
            maxCallbackQueueLength: UInt64? = nil,
            callbackTimeoutNanos: UInt64? = nil
        ) {
            self.executionTimeoutNanos = executionTimeoutNanos
            self.maxInputBytes = maxInputBytes
            self.maxOutputBytes = maxOutputBytes
            self.maxStackDepth = maxStackDepth
            self.maxHeapBytes = maxHeapBytes
            self.maxCallbackQueueLength = maxCallbackQueueLength
            self.callbackTimeoutNanos = callbackTimeoutNanos
        }
    }

    public var executionTimeoutNanos: UInt64?
    public var maxInputBytes: UInt64?
    public var maxOutputBytes: UInt64?
    public var maxStackDepth: UInt64?
    public var maxHeapBytes: UInt64?
    public var maxCallbackQueueLength: UInt64?
    public var callbackTimeoutNanos: UInt64?

    public init(
        executionTimeoutNanos: UInt64? = nil,
        maxInputBytes: UInt64? = nil,
        maxOutputBytes: UInt64? = nil,
        maxStackDepth: UInt64? = nil,
        maxHeapBytes: UInt64? = nil,
        maxCallbackQueueLength: UInt64? = nil,
        callbackTimeoutNanos: UInt64? = nil
    ) {
        self.executionTimeoutNanos = executionTimeoutNanos
        self.maxInputBytes = maxInputBytes
        self.maxOutputBytes = maxOutputBytes
        self.maxStackDepth = maxStackDepth
        self.maxHeapBytes = maxHeapBytes
        self.maxCallbackQueueLength = maxCallbackQueueLength
        self.callbackTimeoutNanos = callbackTimeoutNanos
    }
}

public struct EcritumPermissionPolicy: Equatable, Sendable {
    public enum FilesystemPolicy: Equatable, Sendable {
        case denied
        case readOnly(roots: [FilesystemRoot])
        case readWrite(roots: [FilesystemRoot])
    }

    public struct FilesystemRoot: Hashable, Sendable {
        public let path: String

        public static func directory(_ url: URL) throws -> Self {
            guard url.isFileURL else {
                throw EcritumPolicyValidation.invalidConfig("Filesystem roots must use file URLs")
            }
            let root = FilesystemRoot(path: url.path)
            try EcritumPolicyValidation.validateFilesystemRoot(root)
            return root
        }

        public init(path: String) {
            self.path = path
        }
    }

    public enum NetworkPolicy: Equatable, Sendable {
        case denied
        case allowed(rules: [NetworkRule])

        public static func allowing(_ rules: [NetworkRule]) -> Self {
            .allowed(rules: rules)
        }
    }

    public struct NetworkRule: Hashable, Sendable {
        public let scheme: String
        public let host: String
        public let port: UInt16

        public static func https(host: String, port: UInt16 = 443) -> Self {
            NetworkRule(scheme: "https", host: host, port: port)
        }

        public init(scheme: String, host: String, port: UInt16) {
            self.scheme = scheme
            self.host = host
            self.port = port
        }
    }

    public enum ProcessPolicy: Equatable, Sendable {
        case denied
        case allowed(commands: [Command])
    }

    public struct Command: Hashable, Sendable {
        public let path: String

        public init(path: String) {
            self.path = path
        }
    }

    public enum EnvironmentPolicy: Equatable, Sendable {
        case denied
        case allowed(keys: [String])

        public static func allowing(_ keys: [String]) -> Self {
            .allowed(keys: keys)
        }
    }

    public enum TogglePolicy: Equatable, Sendable {
        case denied
        case allowed
    }

    public struct Narrowing: Equatable, Sendable {
        public var filesystem: FilesystemPolicy?
        public var network: NetworkPolicy?
        public var process: ProcessPolicy?
        public var environment: EnvironmentPolicy?
        public var clock: TogglePolicy?
        public var random: TogglePolicy?
        public var log: TogglePolicy?

        public init(
            filesystem: FilesystemPolicy? = nil,
            network: NetworkPolicy? = nil,
            process: ProcessPolicy? = nil,
            environment: EnvironmentPolicy? = nil,
            clock: TogglePolicy? = nil,
            random: TogglePolicy? = nil,
            log: TogglePolicy? = nil
        ) {
            self.filesystem = filesystem
            self.network = network
            self.process = process
            self.environment = environment
            self.clock = clock
            self.random = random
            self.log = log
        }

        public func withFilesystem(_ filesystem: FilesystemPolicy) -> Self {
            var copy = self
            copy.filesystem = filesystem
            return copy
        }

        public func withNetwork(_ network: NetworkPolicy) -> Self {
            var copy = self
            copy.network = network
            return copy
        }

        public func withProcess(_ process: ProcessPolicy) -> Self {
            var copy = self
            copy.process = process
            return copy
        }

        public func withEnvironment(_ environment: EnvironmentPolicy) -> Self {
            var copy = self
            copy.environment = environment
            return copy
        }

        public func withClock(_ clock: TogglePolicy) -> Self {
            var copy = self
            copy.clock = clock
            return copy
        }

        public func withRandom(_ random: TogglePolicy) -> Self {
            var copy = self
            copy.random = random
            return copy
        }

        public func withLog(_ log: TogglePolicy) -> Self {
            var copy = self
            copy.log = log
            return copy
        }
    }

    public static let defaultDeny = EcritumPermissionPolicy()

    public var filesystem: FilesystemPolicy
    public var network: NetworkPolicy
    public var process: ProcessPolicy
    public var environment: EnvironmentPolicy
    public var clock: TogglePolicy
    public var random: TogglePolicy
    public var log: TogglePolicy

    public init(
        filesystem: FilesystemPolicy = .denied,
        network: NetworkPolicy = .denied,
        process: ProcessPolicy = .denied,
        environment: EnvironmentPolicy = .denied,
        clock: TogglePolicy = .denied,
        random: TogglePolicy = .denied,
        log: TogglePolicy = .denied
    ) {
        self.filesystem = filesystem
        self.network = network
        self.process = process
        self.environment = environment
        self.clock = clock
        self.random = random
        self.log = log
    }

    public func withFilesystem(_ filesystem: FilesystemPolicy) -> Self {
        var copy = self
        copy.filesystem = filesystem
        return copy
    }

    public func withNetwork(_ network: NetworkPolicy) -> Self {
        var copy = self
        copy.network = network
        return copy
    }

    public func withProcess(_ process: ProcessPolicy) -> Self {
        var copy = self
        copy.process = process
        return copy
    }

    public func withEnvironment(_ environment: EnvironmentPolicy) -> Self {
        var copy = self
        copy.environment = environment
        return copy
    }

    public func withClock(_ clock: TogglePolicy) -> Self {
        var copy = self
        copy.clock = clock
        return copy
    }

    public func withRandom(_ random: TogglePolicy) -> Self {
        var copy = self
        copy.random = random
        return copy
    }

    public func withLog(_ log: TogglePolicy) -> Self {
        var copy = self
        copy.log = log
        return copy
    }
}

public extension EcritumRuntime.Configuration {
    func canonicalJSONData() throws -> Data {
        try EcritumPolicyValidation.validateRuntimeConfiguration(self)
        return try EcritumPolicyValidation.configData(EcritumConfigJSON.runtime(self))
    }

    func abiConfigData() throws -> Data {
        self == .default ? Data() : try canonicalJSONData()
    }
}

public extension EcritumContext.Configuration {
    func canonicalJSONData() throws -> Data {
        try EcritumPolicyValidation.validateContextConfiguration(self)
        return try EcritumPolicyValidation.configData(EcritumConfigJSON.context(self))
    }

    func abiConfigData() throws -> Data {
        self == .default ? Data() : try canonicalJSONData()
    }
}

private enum EcritumPolicyValidation {
    private static let maxConfigBytes = 65_536
    private static let maxArrayItems = 256
    private static let maxStringBytes = 4_096

    static func configData(_ json: String) throws -> Data {
        let data = Data(json.utf8)
        guard data.count <= maxConfigBytes else {
            throw invalidConfig("Configuration is too large")
        }
        return data
    }

    static func validateRuntimeConfiguration(_ configuration: EcritumRuntime.Configuration) throws {
        try validateLanguages(configuration.languages)
        try validatePolicy(configuration.policy)
        try validateResourceLimits(configuration.resourceLimits)
    }

    static func validateContextConfiguration(_ configuration: EcritumContext.Configuration) throws {
        try validatePolicyNarrowing(configuration.policy)
        try validateResourceLimits(configuration.resourceLimits)
    }

    static func validateFilesystemRoot(_ root: EcritumPermissionPolicy.FilesystemRoot) throws {
        guard isValidFilesystemPath(root.path) else {
            throw invalidConfig("Invalid filesystem root")
        }
    }

    static func invalidConfig(_ message: String) -> EcritumError {
        EcritumError.invalidConfig(EcritumErrorDetails(
            status: .invalidConfig,
            category: .invalidConfig,
            message: message,
            operation: "configuration_serialize"
        ))
    }

    private static func validateLanguages(_ languages: Set<EcritumLanguage>) throws {
        try validateArrayCount(languages.count, message: "Too many language names")
        for language in languages {
            guard isValidIdentifier(language.rawValue) else {
                throw invalidConfig("Invalid language name")
            }
        }
    }

    private static func validatePolicy(_ policy: EcritumPermissionPolicy) throws {
        try validateFilesystemPolicy(policy.filesystem)
        try validateNetworkPolicy(policy.network)
        try validateProcessPolicy(policy.process)
        try validateEnvironmentPolicy(policy.environment)
    }

    private static func validatePolicyNarrowing(_ policy: EcritumPermissionPolicy.Narrowing) throws {
        if let filesystem = policy.filesystem {
            try validateFilesystemPolicy(filesystem)
        }
        if let network = policy.network {
            try validateNetworkPolicy(network)
        }
        if let process = policy.process {
            try validateProcessPolicy(process)
        }
        if let environment = policy.environment {
            try validateEnvironmentPolicy(environment)
        }
    }

    private static func validateFilesystemPolicy(_ policy: EcritumPermissionPolicy.FilesystemPolicy) throws {
        switch policy {
        case .denied:
            return
        case let .readOnly(roots), let .readWrite(roots):
            guard !roots.isEmpty else {
                throw invalidConfig("Filesystem roots cannot be empty")
            }
            try validateArrayCount(roots.count, message: "Too many filesystem roots")
            try validateUnique(roots.map(\.path), message: "Duplicate filesystem roots")
            for root in roots {
                try validateFilesystemRoot(root)
            }
        }
    }

    private static func validateNetworkPolicy(_ policy: EcritumPermissionPolicy.NetworkPolicy) throws {
        switch policy {
        case .denied:
            return
        case let .allowed(rules):
            guard !rules.isEmpty else {
                throw invalidConfig("Network rules cannot be empty")
            }
            try validateArrayCount(rules.count, message: "Too many network rules")
            let canonicalRules = rules.map { "\($0.scheme)|\($0.host)|\($0.port)" }
            try validateUnique(canonicalRules, message: "Duplicate network rules")
            for rule in rules {
                guard isValidLowerIdentifier(rule.scheme),
                      isValidHost(rule.host),
                      rule.port > 0 else {
                    throw invalidConfig("Invalid network rule")
                }
            }
        }
    }

    private static func validateProcessPolicy(_ policy: EcritumPermissionPolicy.ProcessPolicy) throws {
        switch policy {
        case .denied:
            return
        case let .allowed(commands):
            guard !commands.isEmpty else {
                throw invalidConfig("Process commands cannot be empty")
            }
            try validateArrayCount(commands.count, message: "Too many process commands")
            try validateUnique(commands.map(\.path), message: "Duplicate process commands")
            for command in commands {
                guard isValidProcessPath(command.path) else {
                    throw invalidConfig("Invalid process command")
                }
            }
        }
    }

    private static func validateEnvironmentPolicy(_ policy: EcritumPermissionPolicy.EnvironmentPolicy) throws {
        switch policy {
        case .denied:
            return
        case let .allowed(keys):
            guard !keys.isEmpty else {
                throw invalidConfig("Environment keys cannot be empty")
            }
            try validateArrayCount(keys.count, message: "Too many environment keys")
            try validateUnique(keys, message: "Duplicate environment keys")
            for key in keys {
                guard isValidIdentifier(key) else {
                    throw invalidConfig("Invalid environment key")
                }
            }
        }
    }

    private static func validateResourceLimits(_ limits: EcritumResourceLimits) throws {
        try validateDurationLimit(limits.executionTimeoutNanos)
        try validateDurationLimit(limits.callbackTimeoutNanos)
    }

    private static func validateResourceLimits(_ limits: EcritumResourceLimits.Narrowing) throws {
        try validateDurationLimit(limits.executionTimeoutNanos)
        try validateDurationLimit(limits.callbackTimeoutNanos)
    }

    private static func validateDurationLimit(_ value: UInt64?) throws {
        guard value != UInt64.max else {
            throw invalidConfig("Reserved resource limit value")
        }
    }

    private static func validateUnique(_ values: [String], message: String) throws {
        var seen = Set<String>()
        for value in values {
            guard seen.insert(value).inserted else {
                throw invalidConfig(message)
            }
        }
    }

    private static func validateArrayCount(_ count: Int, message: String) throws {
        guard count <= maxArrayItems else {
            throw invalidConfig(message)
        }
    }

    private static func isValidFilesystemPath(_ path: String) -> Bool {
        guard !path.isEmpty,
              path.utf8.count <= maxStringBytes,
              path.first == "/",
              !path.utf8.contains(0) else {
            return false
        }
        guard path == "/" || !path.hasSuffix("/") else {
            return false
        }
        let components = path.split(separator: "/", omittingEmptySubsequences: false)
        guard !components.dropFirst().contains(where: { $0.isEmpty || $0 == "." || $0 == ".." }) else {
            return false
        }
        return true
    }

    private static func isValidProcessPath(_ path: String) -> Bool {
        guard !path.isEmpty,
              path.utf8.count <= maxStringBytes,
              path.first == "/",
              !path.utf8.contains(0) else {
            return false
        }
        guard path == "/" || !path.hasSuffix("/") else {
            return false
        }
        let components = path.split(separator: "/", omittingEmptySubsequences: false)
        return !components.dropFirst().contains(where: \.isEmpty)
    }

    private static func isValidIdentifier(_ value: String) -> Bool {
        let bytes = Array(value.utf8)
        guard let first = bytes.first, isASCIIAlpha(first), bytes.count <= 255 else {
            return false
        }
        return bytes.dropFirst().allSatisfy { isASCIIAlpha($0) || isASCIIDigit($0) || $0 == UInt8(ascii: "_") }
    }

    private static func isValidLowerIdentifier(_ value: String) -> Bool {
        let bytes = Array(value.utf8)
        guard let first = bytes.first, (UInt8(ascii: "a")...UInt8(ascii: "z")).contains(first), bytes.count <= 255 else {
            return false
        }
        return bytes.dropFirst().allSatisfy {
            (UInt8(ascii: "a")...UInt8(ascii: "z")).contains($0) || isASCIIDigit($0) || $0 == UInt8(ascii: "_")
        }
    }

    private static func isValidHost(_ value: String) -> Bool {
        let bytes = Array(value.utf8)
        guard !bytes.isEmpty, bytes.count <= 255 else {
            return false
        }
        return bytes.allSatisfy { $0 > 0x20 && $0 <= 0x7e && $0 != UInt8(ascii: "*") }
    }

    private static func isASCIIAlpha(_ byte: UInt8) -> Bool {
        (UInt8(ascii: "A")...UInt8(ascii: "Z")).contains(byte) || (UInt8(ascii: "a")...UInt8(ascii: "z")).contains(byte)
    }

    private static func isASCIIDigit(_ byte: UInt8) -> Bool {
        (UInt8(ascii: "0")...UInt8(ascii: "9")).contains(byte)
    }
}

private enum EcritumConfigJSON {
    static func runtime(_ configuration: EcritumRuntime.Configuration) throws -> String {
        let languages = try sortedArray(configuration.languages.map { try jsonString($0.rawValue) })
        return try object([
            member("schemaVersion", "1"),
            member("languages", array(languages)),
            member("policy", policy(configuration.policy)),
            member("diagnostics", diagnostics(configuration.diagnostics)),
            member("resourceLimits", resourceLimits(configuration.resourceLimits)),
        ])
    }

    static func context(_ configuration: EcritumContext.Configuration) throws -> String {
        if configuration == .default {
            return ""
        }

        var members = [member("schemaVersion", "1")]
        if let policy = try contextPolicy(configuration.policy) {
            members.append(member("policy", policy))
        }
        if let limits = resourceLimits(configuration.resourceLimits) {
            members.append(member("resourceLimits", limits))
        }
        return object(members)
    }

    private static func policy(_ policy: EcritumPermissionPolicy) throws -> String {
        try object([
            member("filesystem", filesystem(policy.filesystem)),
            member("network", network(policy.network)),
            member("process", process(policy.process)),
            member("environment", environment(policy.environment)),
            member("clock", toggle(policy.clock)),
            member("random", toggle(policy.random)),
            member("log", toggle(policy.log)),
        ])
    }

    private static func contextPolicy(_ policy: EcritumPermissionPolicy.Narrowing) throws -> String? {
        var members: [String] = []
        if let filesystem = policy.filesystem {
            members.append(member("filesystem", try self.filesystem(filesystem)))
        }
        if let network = policy.network {
            members.append(member("network", try self.network(network)))
        }
        if let process = policy.process {
            members.append(member("process", try self.process(process)))
        }
        if let environment = policy.environment {
            members.append(member("environment", try self.environment(environment)))
        }
        if let clock = policy.clock {
            members.append(member("clock", toggle(clock)))
        }
        if let random = policy.random {
            members.append(member("random", toggle(random)))
        }
        if let log = policy.log {
            members.append(member("log", toggle(log)))
        }
        return members.isEmpty ? nil : object(members)
    }

    private static func filesystem(_ policy: EcritumPermissionPolicy.FilesystemPolicy) throws -> String {
        switch policy {
        case .denied:
            return object([member("mode", try jsonString("denied"))])
        case let .readOnly(roots):
            let rootJSON = try roots.map(root)
            return try object([
                member("mode", jsonString("read_only")),
                member("roots", array(sortedArray(rootJSON))),
            ])
        case let .readWrite(roots):
            let rootJSON = try roots.map(root)
            return try object([
                member("mode", jsonString("read_write")),
                member("roots", array(sortedArray(rootJSON))),
            ])
        }
    }

    private static func root(_ root: EcritumPermissionPolicy.FilesystemRoot) throws -> String {
        try object([
            member("kind", jsonString("directory")),
            member("path", jsonString(root.path)),
        ])
    }

    private static func network(_ policy: EcritumPermissionPolicy.NetworkPolicy) throws -> String {
        switch policy {
        case .denied:
            return object([member("mode", try jsonString("denied"))])
        case let .allowed(rules):
            let ruleJSON = try rules.map(rule)
            return try object([
                member("mode", jsonString("allowed")),
                member("rules", array(sortedArray(ruleJSON))),
            ])
        }
    }

    private static func rule(_ rule: EcritumPermissionPolicy.NetworkRule) throws -> String {
        try object([
            member("scheme", jsonString(rule.scheme)),
            member("host", jsonString(rule.host)),
            member("port", String(rule.port)),
        ])
    }

    private static func process(_ policy: EcritumPermissionPolicy.ProcessPolicy) throws -> String {
        switch policy {
        case .denied:
            return object([member("mode", try jsonString("denied"))])
        case let .allowed(commands):
            let commandJSON = try commands.map(command)
            return try object([
                member("mode", jsonString("allowed")),
                member("commands", array(sortedArray(commandJSON))),
            ])
        }
    }

    private static func command(_ command: EcritumPermissionPolicy.Command) throws -> String {
        try object([member("path", jsonString(command.path))])
    }

    private static func environment(_ policy: EcritumPermissionPolicy.EnvironmentPolicy) throws -> String {
        switch policy {
        case .denied:
            return object([member("mode", try jsonString("denied"))])
        case let .allowed(keys):
            let keyJSON = try keys.map(jsonString)
            return try object([
                member("mode", jsonString("allowed")),
                member("keys", array(sortedArray(keyJSON))),
            ])
        }
    }

    private static func toggle(_ policy: EcritumPermissionPolicy.TogglePolicy) -> String {
        switch policy {
        case .denied:
            return #"{"mode":"denied"}"#
        case .allowed:
            return #"{"mode":"allowed"}"#
        }
    }

    private static func diagnostics(_ policy: EcritumDiagnosticsPolicy) throws -> String {
        switch policy {
        case .redacted:
            return object([member("mode", try jsonString("redacted"))])
        case .raw:
            return object([member("mode", try jsonString("raw"))])
        }
    }

    private static func resourceLimits(_ limits: EcritumResourceLimits) -> String {
        object(resourceLimitMembers(
            executionTimeoutNanos: limits.executionTimeoutNanos,
            maxInputBytes: limits.maxInputBytes,
            maxOutputBytes: limits.maxOutputBytes,
            maxStackDepth: limits.maxStackDepth,
            maxHeapBytes: limits.maxHeapBytes,
            maxCallbackQueueLength: limits.maxCallbackQueueLength,
            callbackTimeoutNanos: limits.callbackTimeoutNanos
        ))
    }

    private static func resourceLimits(_ limits: EcritumResourceLimits.Narrowing) -> String? {
        let members = resourceLimitMembers(
            executionTimeoutNanos: limits.executionTimeoutNanos,
            maxInputBytes: limits.maxInputBytes,
            maxOutputBytes: limits.maxOutputBytes,
            maxStackDepth: limits.maxStackDepth,
            maxHeapBytes: limits.maxHeapBytes,
            maxCallbackQueueLength: limits.maxCallbackQueueLength,
            callbackTimeoutNanos: limits.callbackTimeoutNanos
        )
        return members.isEmpty ? nil : object(members)
    }

    private static func resourceLimitMembers(
        executionTimeoutNanos: UInt64?,
        maxInputBytes: UInt64?,
        maxOutputBytes: UInt64?,
        maxStackDepth: UInt64?,
        maxHeapBytes: UInt64?,
        maxCallbackQueueLength: UInt64?,
        callbackTimeoutNanos: UInt64?
    ) -> [String] {
        var members: [String] = []
        if let executionTimeoutNanos {
            members.append(member("executionTimeoutNanos", String(executionTimeoutNanos)))
        }
        if let maxInputBytes {
            members.append(member("maxInputBytes", String(maxInputBytes)))
        }
        if let maxOutputBytes {
            members.append(member("maxOutputBytes", String(maxOutputBytes)))
        }
        if let maxStackDepth {
            members.append(member("maxStackDepth", String(maxStackDepth)))
        }
        if let maxHeapBytes {
            members.append(member("maxHeapBytes", String(maxHeapBytes)))
        }
        if let maxCallbackQueueLength {
            members.append(member("maxCallbackQueueLength", String(maxCallbackQueueLength)))
        }
        if let callbackTimeoutNanos {
            members.append(member("callbackTimeoutNanos", String(callbackTimeoutNanos)))
        }
        return members
    }

    private static func sortedArray(_ values: [String]) -> [String] {
        values.sorted()
    }

    private static func array(_ values: [String]) -> String {
        "[" + values.joined(separator: ",") + "]"
    }

    private static func object(_ members: [String]) -> String {
        "{" + members.joined(separator: ",") + "}"
    }

    private static func member(_ key: String, _ value: String) -> String {
        #""\#(key)":\#(value)"#
    }

    private static func jsonString(_ value: String) throws -> String {
        var result = "\""
        for scalar in value.unicodeScalars {
            switch scalar.value {
            case 0x22:
                result += "\\\""
            case 0x5c:
                result += "\\\\"
            case 0x08:
                result += "\\b"
            case 0x0c:
                result += "\\f"
            case 0x0a:
                result += "\\n"
            case 0x0d:
                result += "\\r"
            case 0x09:
                result += "\\t"
            case 0x00...0x1f:
                result += String(format: "\\u%04x", scalar.value)
            default:
                result.unicodeScalars.append(scalar)
            }
        }
        result += "\""
        return result
    }
}
