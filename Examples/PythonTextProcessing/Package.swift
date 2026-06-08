// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "PythonTextProcessing",
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(path: "../.."),
    ],
    targets: [
        .executableTarget(
            name: "PythonTextProcessing",
            dependencies: [
                .product(name: "Ecritum", package: "Ecritum"),
            ]
        ),
    ]
)
