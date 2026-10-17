// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "swift-osdi",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "COSDI", targets: ["COSDI"]),
        .library(name: "OSDIContracts", targets: ["OSDIContracts"]),
        .library(name: "OSDIRuntime", targets: ["OSDIRuntime"]),
        .executable(name: "osdi-inspect", targets: ["OSDIInspect"]),
        .executable(name: "osdi-evaluate", targets: ["OSDIEvaluate"])
    ],
    dependencies: [
        .package(url: "https://github.com/1amageek/CircuiteFoundation.git",
                 revision: "ae88104c556f5802819713ed741f5b2fffffc2d9")
    ],
    targets: [
        .target(name: "COSDI", exclude: ["DESIGN.md", "Loading/DESIGN.md", "DeviceEvaluation/DESIGN.md"],
                publicHeadersPath: "include"),
        .target(name: "OSDIContracts", dependencies: [
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation")
        ], exclude: ["DESIGN.md", "LibraryMetadata/DESIGN.md", "ModelEvaluation/DESIGN.md"]),
        .target(name: "OSDIRuntime", dependencies: ["COSDI", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")
        ], exclude: ["DESIGN.md", "LibraryLoading/DESIGN.md", "StableStorage/DESIGN.md", "PointEvaluation/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .executableTarget(name: "OSDIInspect", dependencies: ["OSDIRuntime", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .executableTarget(name: "OSDIEvaluate", dependencies: ["OSDIRuntime", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .testTarget(name: "OSDIEvaluationTests", dependencies: ["OSDIRuntime", "OSDIContracts", "COSDI",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md", "Fixtures", "FixtureSupport/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .testTarget(name: "OSDIRuntimeTests", dependencies: ["OSDIRuntime", "OSDIContracts", "COSDI",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md", "Fixtures", "FixtureSupport/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")])
    ],
    swiftLanguageModes: [.v6]
)
