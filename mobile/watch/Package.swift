// swift-tools-version: 6.0
// DeskGatewayWatchCore isolates protocol and Crown safety logic from watchOS UI APIs.

import PackageDescription

let package = Package(
    name: "DeskGatewayWatchCore",
    platforms: [
        .macOS(.v14),
        .watchOS(.v11),
    ],
    products: [
        .library(
            name: "DeskGatewayWatchCore",
            targets: ["DeskGatewayWatchCore"]
        ),
    ],
    targets: [
        .target(name: "DeskGatewayWatchCore"),
        .testTarget(
            name: "DeskGatewayWatchCoreTests",
            dependencies: ["DeskGatewayWatchCore"]
        ),
    ]
)
