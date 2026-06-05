// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "MacSmokeApp",
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(path: "../.."),
    ],
    targets: [
        .executableTarget(
            name: "EcritumSmokeApp",
            dependencies: [
                .product(name: "Ecritum", package: "Ecritum"),
            ]
        ),
    ]
)
