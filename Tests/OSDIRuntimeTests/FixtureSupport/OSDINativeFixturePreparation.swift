import Foundation
import OSDITestSupport

/// Owns ordinary metadata recipes and one run-local lifetime observation root.
enum OSDINativeFixturePreparation {
    static let names = [
        "valid_two", "extended_stride", "wrong_major", "wrong_minor", "missing_required_symbol",
        "undersized_stride", "excessive_descriptors", "excessive_nodes", "invalid_mapping_offset",
        "overflow_jacobian_count", "misaligned_mapping_offset", "invalid_jacobian_node",
        "invalid_terminal_count", "null_nodes", "missing_eval", "invalid_residual_offset",
        "misaligned_jacobian_pointer", "null_jacobian_entries", "excessive_storage",
        "valid_ground_pair", "invalid_ground_first_node", "missing_setup_model",
        "valid_overprovisioned_jacobian_capacity", "underprovided_jacobian_capacity",
        "valid_capacity_above_final_jacobian_count", "excessive_jacobian_capacity"
    ]

    static func prepare(root: URL) async throws -> OSDIFixtureSet {
        let compiler = try await FixtureCompiler.selected()
        let cacheDirectory = ProcessInfo.processInfo.environment["OSDI_TEST_CACHE_DIRECTORY"]
            .map { URL(fileURLWithPath: $0) } ?? root.appendingPathComponent(".build/osdi-fixture-cache-v1")
        let cache = FixtureBuildCache(directory: cacheDirectory)
        let source = root.appendingPathComponent("Tests/OSDIRuntimeTests/Fixtures/metadata.c")
        let header = root.appendingPathComponent("Sources/COSDI/ABI/osdi_0_4.h")
        guard try FixtureBuildCache.digest(header) == "0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f" else {
            throw FixtureBuildError.invalidInput("Pinned OSDI header identity mismatch")
        }
        let run = FileManager.default.temporaryDirectory.appendingPathComponent("osdi-metadata-run-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: run, withIntermediateDirectories: false)
        try Data("osdi.fixture-run.v1\n".utf8).write(to: run.appendingPathComponent(".osdi-fixture-run"))
        let requests = (0...names.count).map { index in
            let wrongArchitecture = index == names.count
            let name = wrongArchitecture ? "wrong_architecture" : names[index]
            let flags = compiler.arguments(architecture: wrongArchitecture ? "x86_64" : "arm64")
                + ["-dynamiclib", "-fvisibility=default", "-I", header.deletingLastPathComponent().path,
                   "-DFIXTURE_KIND=\(wrongArchitecture ? 0 : index)", "-DFIXTURE_LIFETIME_ID=\(index)",
                   "-Wl,-install_name,@rpath/\(name).dylib"]
            return FixtureBuildRequest(compiler: compiler, source: source, dependencies: [header],
                arguments: flags, fileName: name + ".dylib")
        }
        let artifacts = try await cache.artifacts(requests)
        var libraries: [String: OSDIFixtureManifest.Library] = [:]
        var compiled = 0
        for (index, artifact) in artifacts.enumerated() {
            let wrongArchitecture = index == names.count
            let name = wrongArchitecture ? "wrong_architecture" : names[index]
            if artifact.wasBuilt { compiled += 1 }
            let file = try artifact.materialize(to: run.appendingPathComponent(name + ".dylib"))
            libraries[name] = .init(path: file.path, sha256: artifact.sha256,
                lifetimeMarker: run.appendingPathComponent("fixture-\(index).lifetime").path,
                kind: wrongArchitecture ? 0 : index)
        }
        print("OSDI metadata fixtures: \(compiled) compiled, \(names.count + 1 - compiled) reused")
        return OSDIFixtureSet(directory: run, manifest: .init(schemaVersion: 1, libraries: libraries, models: [:]))
    }
}
