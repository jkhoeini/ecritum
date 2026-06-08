// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "LuaRulesEngine",
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(path: "../.."),
    ],
    targets: [
        .executableTarget(
            name: "LuaRulesEngine",
            dependencies: [
                .product(name: "Ecritum", package: "Ecritum"),
            ]
        ),
    ]
)
