import CEcritum
import XCTest
@testable import Ecritum

final class EcritumTests: XCTestCase {
    func testCEcritumVersionStubLinksBeforeLocalArtifactExists() {
        guard !Ecritum.runtimeArtifactAvailable else {
            return
        }

        var buffer = [CChar](repeating: 0, count: Int(ECRITUM_VERSION_BUFFER_SIZE))
        let status = buffer.withUnsafeMutableBufferPointer { pointer in
            ecritum_version(pointer.baseAddress, pointer.count)
        }

        XCTAssertEqual(status, ECRITUM_ERROR_RUNTIME_UNAVAILABLE)
        XCTAssertEqual(String(cString: buffer), "")
    }

    func testCEcritumVersionStubRejectsInvalidArgumentsBeforeLocalArtifactExists() {
        guard !Ecritum.runtimeArtifactAvailable else {
            return
        }

        XCTAssertEqual(ecritum_version(nil, 0), ECRITUM_ERROR_INVALID_ARGUMENT)
    }

    func testVersionReportsMissingRuntimeBeforeLocalArtifactExists() {
        guard !Ecritum.runtimeArtifactAvailable else {
            return
        }

        XCTAssertThrowsError(try Ecritum.version) { error in
            XCTAssertEqual(error as? EcritumError, .runtimeArtifactMissing)
        }
    }

    func testVersionReturnsValueWhenRuntimeArtifactExists() throws {
        guard Ecritum.runtimeArtifactAvailable else {
            throw XCTSkip("Run `mise exec -- just xcframework` before testing the runtime wrapper path.")
        }

        XCTAssertFalse(try Ecritum.version.isEmpty)
    }
}
