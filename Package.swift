// swift-tools-version: 5.9

import Foundation
import PackageDescription

let localRuntimePath = "dist/local/EcritumRuntime.xcframework"
let packageDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let localRuntimeFullPath = packageDirectory + "/" + localRuntimePath
let localRuntimeMode = ProcessInfo.processInfo.environment["ECRITUM_LOCAL_RUNTIME"]
let localRuntimeState = ProcessInfo.processInfo.environment["ECRITUM_LOCAL_RUNTIME_STATE"]
let hasLocalRuntimeFile = FileManager.default.fileExists(atPath: localRuntimeFullPath)
let forceScaffoldRuntime = localRuntimeMode == "0"
let releaseRuntimeRequired = ProcessInfo.processInfo.environment["ECRITUM_RELEASE_RUNTIME_REQUIRED"] == "1"
let releaseRuntimeURL = ProcessInfo.processInfo.environment["ECRITUM_RUNTIME_URL"]
let releaseRuntimeChecksum = ProcessInfo.processInfo.environment["ECRITUM_RUNTIME_CHECKSUM"]
let hasPartialReleaseRuntime = (releaseRuntimeURL == nil) != (releaseRuntimeChecksum == nil)
if hasPartialReleaseRuntime {
    fatalError("ECRITUM_RUNTIME_URL and ECRITUM_RUNTIME_CHECKSUM must be set together")
}

let hasLocalRuntime: Bool
if releaseRuntimeRequired {
    hasLocalRuntime = false
} else if localRuntimeMode == "1" {
    hasLocalRuntime = localRuntimeState?.hasPrefix("v4:runtime:runtime-present:") == true
        && hasLocalRuntimeFile
} else if forceScaffoldRuntime {
    hasLocalRuntime = false
} else {
    hasLocalRuntime = hasLocalRuntimeFile
}

let hasReleaseRuntime = !forceScaffoldRuntime && releaseRuntimeURL != nil && releaseRuntimeChecksum != nil
if releaseRuntimeRequired && !hasReleaseRuntime {
    fatalError("ECRITUM_RELEASE_RUNTIME_REQUIRED requires ECRITUM_RUNTIME_URL and ECRITUM_RUNTIME_CHECKSUM")
}

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
} else if hasReleaseRuntime, let releaseRuntimeURL, let releaseRuntimeChecksum {
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
        dependencies: ["Ecritum"],
        swiftSettings: swiftSettings
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
