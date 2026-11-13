// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "OnePageKit",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "OnePageKit", targets: ["OnePageKit"]),
    ],
    targets: [
        .target(name: "OnePageKit"),
        .testTarget(
            name: "OnePageKitTests",
            dependencies: ["OnePageKit"],
            exclude: ["Fixtures"]
        ),
    ]
)
