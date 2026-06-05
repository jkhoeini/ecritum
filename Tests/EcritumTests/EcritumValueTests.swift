import Foundation
import XCTest
@testable import Ecritum

final class EcritumValueTests: XCTestCase {
    func testRepresentsAllM2ValueKinds() {
        let data = Data([0x00, 0x01, 0xff])

        let value = EcritumValue.object([
            "null": .null,
            "bool": .bool(true),
            "int": .int(42),
            "double": .double(2.5),
            "string": .string("hello"),
            "data": .data(data),
            "array": .array([.null, .bool(false), .int(-1)]),
            "object": .object(["nested": .string("value")]),
        ])

        XCTAssertEqual(value, .object([
            "null": .null,
            "bool": .bool(true),
            "int": .int(42),
            "double": .double(2.5),
            "string": .string("hello"),
            "data": .data(data),
            "array": .array([.null, .bool(false), .int(-1)]),
            "object": .object(["nested": .string("value")]),
        ]))
    }

    func testValueModelIsPureSwiftData() {
        let bytes = Data([1, 2, 3])
        let value = EcritumValue.array([.data(bytes), .string("own\u{0}ed")])

        XCTAssertEqual(value, .array([.data(Data([1, 2, 3])), .string("own\u{0}ed")]))
    }

    func testIntegerAndDoubleRemainDistinctValueKinds() {
        XCTAssertNotEqual(EcritumValue.int(1), EcritumValue.double(1))
    }

    func testConvertsEachValueKindToSwiftNativeValue() {
        let data = Data([4, 5, 6])
        let array: [EcritumValue] = [.int(1), .string("two")]
        let object: [String: EcritumValue] = ["answer": .int(42)]

        XCTAssertTrue(EcritumValue.null.isNull)
        XCTAssertEqual(EcritumValue.bool(true).boolValue, true)
        XCTAssertEqual(EcritumValue.int(42).intValue, 42)
        XCTAssertEqual(EcritumValue.double(1.25).doubleValue, 1.25)
        XCTAssertEqual(EcritumValue.string("text").stringValue, "text")
        XCTAssertEqual(EcritumValue.data(data).dataValue, data)
        XCTAssertEqual(EcritumValue.array(array).arrayValue, array)
        XCTAssertEqual(EcritumValue.object(object).objectValue, object)
    }

    func testWrongKindConversionReturnsNil() {
        XCTAssertNil(EcritumValue.null.boolValue)
        XCTAssertNil(EcritumValue.bool(true).intValue)
        XCTAssertNil(EcritumValue.int(1).doubleValue)
        XCTAssertNil(EcritumValue.double(1).intValue)
        XCTAssertNil(EcritumValue.string("text").dataValue)
        XCTAssertNil(EcritumValue.data(Data()).stringValue)
        XCTAssertNil(EcritumValue.array([]).objectValue)
        XCTAssertNil(EcritumValue.object([:]).arrayValue)
    }
}
