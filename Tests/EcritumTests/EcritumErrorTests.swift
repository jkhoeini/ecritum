import CEcritum
import XCTest
@testable import Ecritum

final class EcritumErrorTests: XCTestCase {
    func testStatusConstantsMapFromCABIToSwiftCases() {
        let expected: [(Int32, EcritumStatus)] = [
            (Int32(ECRITUM_OK), .ok),
            (Int32(ECRITUM_ERROR_INVALID_ARGUMENT), .invalidArgument),
            (Int32(ECRITUM_ERROR_BUFFER_TOO_SMALL), .bufferTooSmall),
            (Int32(ECRITUM_ERROR_RUNTIME_UNAVAILABLE), .runtimeUnavailable),
            (Int32(ECRITUM_ERROR_INVALID_HANDLE), .invalidHandle),
            (Int32(ECRITUM_ERROR_OUT_OF_MEMORY), .outOfMemory),
            (Int32(ECRITUM_ERROR_INVALID_UTF8), .invalidUTF8),
            (Int32(ECRITUM_ERROR_INPUT_TOO_LARGE), .inputTooLarge),
            (Int32(ECRITUM_ERROR_INVALID_CONFIG), .invalidConfig),
            (Int32(ECRITUM_ERROR_UNSUPPORTED_CONFIG_VERSION), .unsupportedConfigVersion),
            (Int32(ECRITUM_ERROR_CONTEXTS_ALIVE), .contextsAlive),
            (Int32(ECRITUM_ERROR_CLOSED), .closed),
            (Int32(ECRITUM_ERROR_BUSY), .busy),
            (Int32(ECRITUM_ERROR_REENTRANT_CALL), .reentrantCall),
            (Int32(ECRITUM_ERROR_PERMISSION_DENIED), .permissionDenied),
            (Int32(ECRITUM_ERROR_TIMEOUT), .timeout),
            (Int32(ECRITUM_ERROR_CANCELLED), .cancelled),
            (Int32(ECRITUM_ERROR_SCRIPT), .script),
            (Int32(ECRITUM_ERROR_CALLBACK), .callback),
            (Int32(ECRITUM_ERROR_TEARDOWN_FAILED), .teardownFailed),
            (Int32(ECRITUM_ERROR_INTERNAL), .internalFailure),
            (Int32(ECRITUM_ERROR_ALREADY_EXISTS), .alreadyExists),
        ]

        XCTAssertEqual(EcritumStatus.allCases.count, expected.count)
        for (rawValue, status) in expected {
            XCTAssertEqual(EcritumStatus(rawValue: rawValue), status)
            XCTAssertEqual(status.rawValue, rawValue)
        }
    }

    func testStructuredErrorPreservesSafeDiagnostics() {
        let frame = EcritumStackFrame(
            function: "handler",
            sourceName: "guest.cljs",
            line: 10,
            column: 4
        )
        let details = EcritumErrorDetails(
            status: .script,
            category: .script,
            message: "script evaluation failed",
            operation: "eval",
            language: "clojure",
            sourceName: "guest.cljs",
            line: 10,
            column: 4,
            stack: [frame]
        )

        let error = EcritumError.script(details)

        XCTAssertEqual(error.status, .script)
        XCTAssertEqual(error.category, .script)
        XCTAssertEqual(error.details, details)
        XCTAssertEqual(error.errorDescription, "script evaluation failed")
        XCTAssertFalse(String(describing: error).contains("java.lang"))
        XCTAssertFalse(String(describing: error).contains("Throwable"))
    }

    func testEveryKnownFailureStatusMapsToTypedErrorCase() {
        let cases: [(EcritumStatus, EcritumError)] = [
            (.invalidArgument, .invalidArgument(.fixture(.invalidArgument))),
            (.invalidHandle, .invalidHandle(.fixture(.invalidHandle))),
            (.bufferTooSmall, .bufferTooSmall(.fixture(.bufferTooSmall))),
            (.outOfMemory, .outOfMemory(.fixture(.outOfMemory))),
            (.invalidUTF8, .invalidUTF8(.fixture(.invalidUTF8))),
            (.inputTooLarge, .inputTooLarge(.fixture(.inputTooLarge))),
            (.invalidConfig, .invalidConfig(.fixture(.invalidConfig))),
            (.unsupportedConfigVersion, .unsupportedConfigVersion(.fixture(.unsupportedConfigVersion))),
            (.contextsAlive, .contextsAlive(.fixture(.contextsAlive))),
            (.closed, .closed(.fixture(.closed))),
            (.busy, .busy(.fixture(.busy))),
            (.reentrantCall, .reentrantCall(.fixture(.reentrantCall))),
            (.permissionDenied, .permissionDenied(.fixture(.permissionDenied))),
            (.timeout, .timeout(.fixture(.timeout))),
            (.cancelled, .cancelled(.fixture(.cancelled))),
            (.script, .script(.fixture(.script))),
            (.callback, .callback(.fixture(.callback))),
            (.runtimeUnavailable, .runtimeUnavailable(.fixture(.runtimeUnavailable))),
            (.teardownFailed, .teardownFailed(.fixture(.teardownFailed))),
            (.internalFailure, .internalFailure(.fixture(.internalFailure))),
            (.alreadyExists, .alreadyExists(.fixture(.alreadyExists))),
        ]

        for (status, expected) in cases {
            XCTAssertEqual(EcritumError.from(status: status.rawValue, details: .fixture(status)), expected)
        }
    }

    func testUnknownStatusPreservesRawCode() {
        let details = EcritumErrorDetails(
            status: nil,
            category: .unknown,
            message: "runtime returned unknown status",
            operation: "version"
        )

        XCTAssertEqual(
            EcritumError.from(status: 999, details: details),
            .unknownStatus(rawStatus: 999, details: details)
        )
    }

    func testOkStatusMapsToUnknownStatusBecauseItIsNotAnError() {
        XCTAssertEqual(EcritumError.from(status: EcritumStatus.ok.rawValue), .unknownStatus(rawStatus: 0, details: nil))
    }

    func testRuntimeArtifactMissingHasStructuredAccessors() {
        let error = EcritumError.runtimeArtifactMissing

        XCTAssertNil(error.status)
        XCTAssertEqual(error.category, .runtimeArtifactMissing)
        XCTAssertNil(error.details)
        XCTAssertEqual(error.errorDescription, "Ecritum runtime artifact is not available")
    }

    func testUnsafeDiagnosticMessagesAreRedacted() {
        let details = EcritumErrorDetails(
            status: .internalFailure,
            category: .internalFailure,
            message: "java.lang.RuntimeException at org.graalvm.Polyglot /Users/example/project SECRET_TOKEN=abc",
            operation: "ProcessBuilder exec",
            language: "org.graalvm.polyglot",
            sourceName: "/Users/example/project/guest.clj",
            stack: [
                EcritumStackFrame(
                    function: "java.lang.Throwable",
                    sourceName: "/Users/example/project/stack.clj"
                ),
            ]
        )

        let error = EcritumError.internalFailure(details)

        XCTAssertEqual(error.errorDescription, "Ecritum operation failed")
        XCTAssertFalse(String(describing: error).contains("java.lang"))
        XCTAssertFalse(String(describing: error).contains("org.graalvm"))
        XCTAssertFalse(String(describing: error).contains("/Users/example"))
        XCTAssertFalse(String(describing: error).contains("SECRET_TOKEN"))
        XCTAssertFalse(String(describing: error).contains("ProcessBuilder"))
        XCTAssertNil(error.details?.operation)
        XCTAssertNil(error.details?.language)
        XCTAssertNil(error.details?.sourceName)
        XCTAssertNil(error.details?.stack.first?.function)
        XCTAssertNil(error.details?.stack.first?.sourceName)
    }

    func testWhitespaceDiagnosticMessageUsesFallback() {
        let details = EcritumErrorDetails(
            status: .internalFailure,
            category: .internalFailure,
            message: " \n\t ",
            operation: "eval"
        )

        XCTAssertEqual(details.message, "Ecritum operation failed")
    }
}

private extension EcritumErrorDetails {
    static func fixture(_ status: EcritumStatus) -> EcritumErrorDetails {
        EcritumErrorDetails(
            status: status,
            category: EcritumErrorCategory(status: status),
            message: "\(status) failed",
            operation: "test"
        )
    }
}
