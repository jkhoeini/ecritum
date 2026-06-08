// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MultiLanguageHost",
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(path: "../.."),
    ],
    targets: [
        .executableTarget(
            name: "MultiLanguageHost",
            dependencies: [
                .product(name: "Ecritum", package: "Ecritum"),
            ]
        ),
    ]
)
