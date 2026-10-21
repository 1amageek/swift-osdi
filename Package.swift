// swift-tools-version: 6.4
import PackageDescription

let package = Package(
    name: "swift-osdi",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "OSDIContracts", targets: ["OSDIContracts"]),
        .library(name: "OSDIRuntime", targets: ["OSDIRuntime"]),
        .executable(name: "osdi-precision-worker", targets: ["OSDIPrecisionWorker"]),
        .executable(name: "osdi", targets: ["OSDICLI"])
    ],
    dependencies: [
        .package(url: "https://github.com/1amageek/CircuiteFoundation.git",
                 exact: "26.1021.1"),
        .package(url: "https://github.com/1amageek/SignoffToolSupport.git",
                 exact: "26.1018.0")
    ],
    targets: [
        .systemLibrary(name: "CGMP", pkgConfig: "gmp", providers: [.brew(["gmp"])]),
        .systemLibrary(name: "CMPFR", pkgConfig: "mpfr", providers: [.brew(["mpfr"])]),
        .target(name: "COSDI", exclude: ["DESIGN.md", "Loading/DESIGN.md", "DeviceEvaluation/DESIGN.md",
                "CompilerPrecision/DESIGN.md", "PrecisionProcess/DESIGN.md", "PrecisionProcess/DecisionCover/DESIGN.md", "PrecisionProcess/ConstitutiveIdentities/DESIGN.md"],
                publicHeadersPath: "include"),
        .executableTarget(name: "OSDIPrecisionWorker", dependencies: ["COSDI", "CMPFR", "CGMP"],
                exclude: ["DESIGN.md", "Arena/DESIGN.md", "Arithmetic/DESIGN.md",
                    "ProgramEvaluation/DESIGN.md", "ConstitutiveIdentities/DESIGN.md", "DecisionCover/DESIGN.md", "WorkerProtocol/DESIGN.md", "WorkerProtocol/DecisionCover/DESIGN.md", "WorkerProtocol/ConstitutiveIdentities/DESIGN.md"]),
        .target(name: "OSDIContracts", exclude: ["DESIGN.md", "LibraryMetadata/DESIGN.md", "ModelEvaluation/DESIGN.md",
                    "PrecisionEvaluation/DESIGN.md", "DecisionCover/DESIGN.md", "ConstitutiveIdentities/DESIGN.md"]),
        .target(name: "OSDIRuntime", dependencies: ["COSDI", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")
        ], exclude: ["DESIGN.md", "LibraryLoading/DESIGN.md", "StableStorage/DESIGN.md", "PointEvaluation/DESIGN.md",
                    "PrecisionExecution/DESIGN.md", "SnapshotConstruction/DESIGN.md", "DecisionCover/DESIGN.md", "ConstitutiveIdentities/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .executableTarget(name: "OSDICLI", dependencies: ["OSDIRuntime", "OSDIContracts",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md", "Inspection/DESIGN.md", "PointEvaluation/DESIGN.md", "PrecisionEvaluation/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .target(name: "OSDITestSupport", dependencies: [
            .product(name: "SignoffToolSupport", package: "SignoffToolSupport")],
                path: "Tests/OSDITestSupport", exclude: ["DESIGN.md", "BuildCache/DESIGN.md"]),
        .testTarget(name: "OSDIEvaluationTests", dependencies: ["OSDITestSupport", "OSDIRuntime", "OSDIContracts", "COSDI",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md", "Fixtures", "FixtureSupport/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")]),
        .testTarget(name: "OSDIRuntimeTests", dependencies: ["OSDITestSupport",
            .product(name: "SignoffToolSupport", package: "SignoffToolSupport"), "OSDIRuntime", "OSDIContracts", "COSDI",
            .product(name: "CircuiteFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationCrypto", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFoundation", package: "CircuiteFoundation"),
            .product(name: "CircuiteFoundationFileSystem", package: "CircuiteFoundation")],
                exclude: ["DESIGN.md", "Fixtures", "FixtureSupport/DESIGN.md"],
                swiftSettings: [.enableExperimentalFeature("Lifetimes"), .enableExperimentalFeature("LifetimeDependence")])
    ],
    swiftLanguageModes: [.v6]
)
