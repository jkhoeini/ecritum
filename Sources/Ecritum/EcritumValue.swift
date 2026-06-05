import Foundation

/// Swift-native representation of values that can cross the Ecritum boundary.
public enum EcritumValue: Sendable, Equatable {
    case null
    case bool(Bool)
    case int(Int64)
    case double(Double)
    case string(String)
    case data(Data)
    case array([EcritumValue])
    case object([String: EcritumValue])
}

public extension EcritumValue {
    var isNull: Bool {
        if case .null = self {
            return true
        }
        return false
    }

    var boolValue: Bool? {
        if case let .bool(value) = self {
            return value
        }
        return nil
    }

    var intValue: Int64? {
        if case let .int(value) = self {
            return value
        }
        return nil
    }

    var doubleValue: Double? {
        if case let .double(value) = self {
            return value
        }
        return nil
    }

    var stringValue: String? {
        if case let .string(value) = self {
            return value
        }
        return nil
    }

    var dataValue: Data? {
        if case let .data(value) = self {
            return value
        }
        return nil
    }

    var arrayValue: [EcritumValue]? {
        if case let .array(value) = self {
            return value
        }
        return nil
    }

    var objectValue: [String: EcritumValue]? {
        if case let .object(value) = self {
            return value
        }
        return nil
    }
}
