import Foundation
import XCTest
@testable import Ecritum

final class EcritumPolicyTests: XCTestCase {
    func testDefaultRuntimeConfigurationSerializesCanonicalDenyByDefaultJSON() throws {
        let json = String(decoding: try EcritumRuntime.Configuration.default.canonicalJSONData(), as: UTF8.self)

        XCTAssertEqual(
            json,
            #"{"schemaVersion":1,"languages":[],"policy":{"filesystem":{"mode":"denied"},"network":{"mode":"denied"},"process":{"mode":"denied"},"environment":{"mode":"denied"},"clock":{"mode":"denied"},"random":{"mode":"denied"},"log":{"mode":"denied"}},"diagnostics":{"mode":"redacted"},"resourceLimits":{}}"#
        )
    }

    func testExplicitRuntimeConfigurationSerializesCanonicalSetsAndResourceLimits() throws {
        let scriptsURL = URL(fileURLWithPath: "/tmp/ecritum/scripts", isDirectory: true)
        let policy = EcritumPermissionPolicy.defaultDeny
            .withFilesystem(.readOnly(roots: [try .directory(scriptsURL)]))
            .withNetwork(.allowing([.https(host: "api.example.com", port: 443)]))
            .withEnvironment(.allowing(["ECRITUM_MODE"]))
            .withLog(.allowed)
        let configuration = EcritumRuntime.Configuration(
            languages: [.javascript, .clojure],
            policy: policy,
            diagnostics: .raw,
            resourceLimits: EcritumResourceLimits(
                executionTimeoutNanos: 1_000_000,
                maxOutputBytes: 1_024
            )
        )

        let json = String(decoding: try configuration.canonicalJSONData(), as: UTF8.self)

        XCTAssertEqual(
            json,
            #"{"schemaVersion":1,"languages":["clojure","javascript"],"policy":{"filesystem":{"mode":"read_only","roots":[{"kind":"directory","path":"/tmp/ecritum/scripts"}]},"network":{"mode":"allowed","rules":[{"scheme":"https","host":"api.example.com","port":443}]},"process":{"mode":"denied"},"environment":{"mode":"allowed","keys":["ECRITUM_MODE"]},"clock":{"mode":"denied"},"random":{"mode":"denied"},"log":{"mode":"allowed"}},"diagnostics":{"mode":"raw"},"resourceLimits":{"executionTimeoutNanos":1000000,"maxOutputBytes":1024}}"#
        )
    }

    func testContextConfigurationSerializesOnlyExplicitNarrowingSections() throws {
        let scriptsURL = URL(fileURLWithPath: "/tmp/ecritum/scripts", isDirectory: true)
        let configuration = EcritumContext.Configuration(
            policy: EcritumPermissionPolicy.Narrowing()
                .withFilesystem(.readOnly(roots: [try .directory(scriptsURL)]))
                .withNetwork(.denied),
            resourceLimits: EcritumResourceLimits.Narrowing(maxOutputBytes: 512)
        )

        let json = String(decoding: try configuration.canonicalJSONData(), as: UTF8.self)

        XCTAssertEqual(
            json,
            #"{"schemaVersion":1,"policy":{"filesystem":{"mode":"read_only","roots":[{"kind":"directory","path":"/tmp/ecritum/scripts"}]},"network":{"mode":"denied"}},"resourceLimits":{"maxOutputBytes":512}}"#
        )
    }

    func testCanonicalSerializationIsStable() throws {
        let first = try EcritumRuntime.Configuration.default.canonicalJSONData()
        let second = try EcritumRuntime.Configuration.default.canonicalJSONData()

        XCTAssertEqual(first, second)
        XCTAssertEqual(EcritumConfigurationSchemaVersion.v1.rawValue, 1)
    }

    func testSwiftSerializationRejectsInvalidPolicyValuesBeforeABIHandoff() throws {
        XCTAssertThrowsError(try EcritumPermissionPolicy.FilesystemRoot.directory(URL(string: "https://example.com/scripts")!)) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidConfig)
        }

        let invalidConfigurations: [EcritumRuntime.Configuration] = [
            .init(languages: [.init(rawValue: "bad-language")]),
            .init(languages: Set((0..<257).map { EcritumLanguage(rawValue: "l\($0)") })),
            .init(policy: .defaultDeny.withFilesystem(.readOnly(roots: []))),
            .init(policy: .defaultDeny.withFilesystem(.readOnly(roots: [.init(path: "/" + String(repeating: "a", count: 4_096))]))),
            .init(policy: .defaultDeny.withFilesystem(.readOnly(roots: [.init(path: "relative")]))),
            .init(policy: .defaultDeny.withFilesystem(.readOnly(roots: [.init(path: "/tmp/ecritum"), .init(path: "/tmp/ecritum")]))),
            .init(policy: .defaultDeny.withNetwork(.allowing([]))),
            .init(policy: .defaultDeny.withNetwork(.allowing((0..<257).map { .init(scheme: "https", host: "api\($0).example.com", port: 443) }))),
            .init(policy: .defaultDeny.withNetwork(.allowing([.init(scheme: "HTTPS", host: "api.example.com", port: 443)]))),
            .init(policy: .defaultDeny.withNetwork(.allowing([.https(host: "*.example.com", port: 443)]))),
            .init(policy: .defaultDeny.withNetwork(.allowing([.https(host: "api.example.com", port: 0)]))),
            .init(policy: .defaultDeny.withNetwork(.allowing([.https(host: "api.example.com", port: 443), .https(host: "api.example.com", port: 443)]))),
            .init(policy: .defaultDeny.withProcess(.allowed(commands: []))),
            .init(policy: .defaultDeny.withProcess(.allowed(commands: (0..<257).map { .init(path: "/usr/bin/tool\($0)") }))),
            .init(policy: .defaultDeny.withProcess(.allowed(commands: [.init(path: "/" + String(repeating: "a", count: 4_096))]))),
            .init(policy: .defaultDeny.withProcess(.allowed(commands: [.init(path: "relative")]))),
            .init(policy: .defaultDeny.withProcess(.allowed(commands: [.init(path: "/usr/bin/true"), .init(path: "/usr/bin/true")]))),
            .init(policy: .defaultDeny.withEnvironment(.allowing([]))),
            .init(policy: .defaultDeny.withEnvironment(.allowing((0..<257).map { "KEY_\($0)" }))),
            .init(policy: .defaultDeny.withEnvironment(.allowing(["BAD-NAME"]))),
            .init(policy: .defaultDeny.withEnvironment(.allowing(["ECRITUM_MODE", "ECRITUM_MODE"]))),
            .init(resourceLimits: EcritumResourceLimits(executionTimeoutNanos: UInt64.max)),
            .init(resourceLimits: EcritumResourceLimits(callbackTimeoutNanos: UInt64.max)),
        ]

        for configuration in invalidConfigurations {
            XCTAssertThrowsError(try configuration.canonicalJSONData()) { error in
                XCTAssertEqual((error as? EcritumError)?.status, .invalidConfig)
            }
        }

        XCTAssertThrowsError(
            try EcritumContext.Configuration(
                policy: EcritumPermissionPolicy.Narrowing()
                    .withFilesystem(.readOnly(roots: []))
            ).canonicalJSONData()
        ) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidConfig)
        }

        let hugeEnvironmentKeys = (0..<256).map { index in
            "K\(String(format: "%03d", index))\(String(repeating: "_", count: 251))"
        }
        XCTAssertThrowsError(
            try EcritumRuntime.Configuration(
                policy: .defaultDeny.withEnvironment(.allowing(hugeEnvironmentKeys))
            ).canonicalJSONData()
        ) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidConfig)
        }

        XCTAssertThrowsError(
            try EcritumContext.Configuration(
                resourceLimits: EcritumResourceLimits.Narrowing(callbackTimeoutNanos: UInt64.max)
            ).canonicalJSONData()
        ) { error in
            XCTAssertEqual((error as? EcritumError)?.status, .invalidConfig)
        }
    }

    func testArtifactBackedRuntimeAcceptsExplicitDefaultConfiguration() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let runtime = try EcritumRuntime(.default)
        let context = try runtime.context(.default)

        try context.close()
        try runtime.close()
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed policy config.")
        #endif
    }

    func testArtifactBackedRuntimeAcceptsNonDefaultPolicyConfigurationAndContextNarrowing() throws {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        let scriptsURL = URL(fileURLWithPath: "/tmp/ecritum/scripts", isDirectory: true)
        let runtime = try EcritumRuntime(
            .init(
                languages: [.javascript],
                policy: .defaultDeny
                    .withFilesystem(.readOnly(roots: [try .directory(scriptsURL)]))
                    .withNetwork(.allowing([.https(host: "api.example.com", port: 443)]))
                    .withLog(.allowed),
                resourceLimits: EcritumResourceLimits(maxOutputBytes: 1_024)
            )
        )
        let context = try runtime.context(
            .init(
                policy: EcritumPermissionPolicy.Narrowing()
                    .withFilesystem(.readOnly(roots: [try .directory(scriptsURL)]))
                    .withNetwork(.denied)
                    .withLog(.denied),
                resourceLimits: EcritumResourceLimits.Narrowing(maxOutputBytes: 512)
            )
        )

        try context.close()
        try runtime.close()
        #else
        throw XCTSkip("Run `mise exec -- just xcframework` before testing artifact-backed non-default policy config.")
        #endif
    }
}
