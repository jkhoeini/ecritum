import Foundation

public struct EcritumScriptOptions: Equatable, Sendable {
    public static let `default` = EcritumScriptOptions()

    public init() {}

    func abiOptionsData() throws -> Data {
        Data()
    }
}

public struct EcritumScript: Equatable, Sendable {
    public var source: String
    public var language: EcritumLanguage
    public var sourceName: String?
    public var options: EcritumScriptOptions

    public init(
        _ source: String,
        language: EcritumLanguage,
        sourceName: String? = nil,
        options: EcritumScriptOptions = .default
    ) {
        self.source = source
        self.language = language
        self.sourceName = sourceName
        self.options = options
    }
}
