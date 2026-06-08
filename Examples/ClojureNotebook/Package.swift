// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "ClojureNotebook",
    platforms: [
        .macOS(.v14),
    ],
    dependencies: [
        .package(path: "../.."),
    ],
    targets: [
        .executableTarget(
            name: "ClojureNotebook",
            dependencies: [
                .product(name: "Ecritum", package: "Ecritum"),
            ]
        ),
    ]
)
