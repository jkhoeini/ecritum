// swift-tools-version: 5.9

import Foundation
import PackageDescription

let localRuntimePath = "dist/local/EcritumRuntime.xcframework"
let packageDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let localRuntimeFullPath = packageDirectory + "/" + localRuntimePath
let hasLocalRuntime = FileManager.default.fileExists(atPath: localRuntimeFullPath)

let releaseRuntimeURL = ProcessInfo.processInfo.environment["ECRITUM_RUNTIME_URL"]
let releaseRuntimeChecksum = ProcessInfo.processInfo.environment["ECRITUM_RUNTIME_CHECKSUM"]
let hasReleaseRuntime = releaseRuntimeURL != nil && releaseRuntimeChecksum != nil

var runtimeDependency: [Target.Dependency] = []
var swiftSettings: [SwiftSetting] = []
var targets: [Target] = []

if hasLocalRuntime {
    targets.append(
        .binaryTarget(
            name: "EcritumRuntime",
            path: localRuntimePath
        )
    )
    runtimeDependency.append("EcritumRuntime")
    swiftSettings.append(.define("ECRITUM_HAS_RUNTIME_ARTIFACT"))
} else if let releaseRuntimeURL, let releaseRuntimeChecksum {
    targets.append(
        .binaryTarget(
            name: "EcritumRuntime",
            url: releaseRuntimeURL,
            checksum: releaseRuntimeChecksum
        )
    )
    runtimeDependency.append("EcritumRuntime")
    swiftSettings.append(.define("ECRITUM_HAS_RUNTIME_ARTIFACT"))
}

targets.append(contentsOf: [
    .target(
        name: "CEcritum",
        publicHeadersPath: "include"
    ),
    .target(
        name: "Ecritum",
        dependencies: ["CEcritum"] + runtimeDependency,
        swiftSettings: swiftSettings
    ),
    .testTarget(
        name: "EcritumTests",
        dependencies: ["Ecritum"]
    ),
])

let package = Package(
    name: "Ecritum",
    platforms: [
        .macOS(.v14),
    ],
    products: [
        .library(
            name: "Ecritum",
            targets: ["Ecritum"]
        ),
    ],
    targets: targets
)

