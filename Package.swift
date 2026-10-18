// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "swift-osdi",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "COSDI", targets: ["COSDI"]),
        .library(name: "OSDIContracts", targets: ["OSDIContracts"]),
        .library(name: "OSDIRuntime", targets: ["OSDIRuntime"]),
        .executable(name: "osdi-precision-worker", targets: ["OSDIPrecisionWorker"]),
        .executable(name: "osdi-inspect", targets: ["OSDIInspect"]),
        .executable(name: "osdi-evaluate", targets: ["OSDIEvaluate"])
    ],
    dependencies: [
        .package(url: "https://github.com/1amageek/CircuiteFoundation.git",
                 revision: "ae88104c556f5802819713ed741f5b2fffffc2d9")
    ],
    targets: [
        .systemLibrary(name: "CGMP", pkgConfig: "gmp", providers: [.brew(["gmp"])]),
        .systemLibrary(name: "CMPFR", pkgConfig: "mpfr", providers: [.brew(["mpfr"])]),
        .target(name: "COSDI", exclude: ["DESIGN.md", "Loading/DESIGN.md", "DeviceEvaluation/DESIGN.md",
                "CompilerPrecision/DESIGN.md", "PrecisionProcess/DESIGN.md"],
                publicHeadersPath: "include"),
        .target(name: "COSDIPrecision", dependencies: ["COSDI", "CMPFR", "CGMP"],
                exclude: ["DESIGN.md", "Arena/DESIGN.md", "Arithmetic/DESIGN.md",
                    "ProgramEvaluation/DESIGN.md", "WorkerProtocol/DESIGN.md"], publicHeadersPath: "include"),
        .executableTarget(name: "OSDIPrecisionWorker", dependencies: ["COSDIPrecision"],
                exclude: ["DESIGN.md"]),
        .target(name: "OSDIContracts", dependencies: [
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation")
        ], exclude: ["DESIGN.md", "LibraryMetadata/DESIGN.md", "ModelEvaluation/DESIGN.md",
                    "PrecisionEvaluation/DESIGN.md"]),
        .target(name: "OSDIRuntime", dependencies: ["COSDI", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")
        ], exclude: ["DESIGN.md", "LibraryLoading/DESIGN.md", "StableStorage/DESIGN.md", "PointEvaluation/DESIGN.md",
                    "PrecisionExecution/DESIGN.md"],
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
