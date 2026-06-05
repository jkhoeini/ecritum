import CEcritum
import XCTest
@testable import Ecritum

final class EcritumTests: XCTestCase {
    func testRuntimeArtifactAvailabilityMatchesBuildConfiguration() {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        XCTAssertTrue(Ecritum.runtimeArtifactAvailable)
        #else
        XCTAssertFalse(Ecritum.runtimeArtifactAvailable)
        #endif
    }

    func testVersionReportsMissingRuntimeBeforeLocalArtifactExists() {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        return
        #else
        guard !Ecritum.runtimeArtifactAvailable else {
            return
        }

        XCTAssertThrowsError(try Ecritum.version) { error in
            XCTAssertEqual(error as? EcritumError, .runtimeArtifactMissing)
        }
        #endif
    }

    func testCEcritumVersionReturnsValueWhenRuntimeArtifactExists() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        var buffer = [CChar](repeating: 0, count: Int(ECRITUM_VERSION_BUFFER_SIZE))
        let status = buffer.withUnsafeMutableBufferPointer { pointer in
            ecritum_version(pointer.baseAddress, pointer.count)
        }

        XCTAssertEqual(status, ECRITUM_OK)
        XCTAssertEqual(String(cString: buffer), "0.1.0-dev")
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing the C runtime path.")
        #endif
    }

    func testVersionReturnsValueWhenRuntimeArtifactExists() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        XCTAssertEqual(try Ecritum.version, "0.1.0-dev")
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing the Swift runtime path.")
        #endif
    }
}
