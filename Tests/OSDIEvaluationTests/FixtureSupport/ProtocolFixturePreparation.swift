import Foundation
import OSDITestSupport

/// Owns protocol/transport fixture recipes; immutable builds are shared across runs.
enum ProtocolFixturePreparation {
    static func prepare(root: URL) async throws -> ProtocolFixtureManifest {
        let names = ["state", "fatal", "finish", "stop", "diagnostics", "diagnostic_overflow",
                     "logged_error", "limiting", "unknown_flags", "nonfinite_output", "negative_noise",
                     "nonfinite_noise", "time_state_charge", "no_charge", "negative_zero_noise"]
        let compiler = try await FixtureCompiler.selected()
        let cacheDirectory = ProcessInfo.processInfo.environment["OSDI_TEST_CACHE_DIRECTORY"]
            .map { URL(fileURLWithPath: $0) } ?? root.appendingPathComponent(".build/osdi-fixture-cache-v1")
        let cache = FixtureBuildCache(directory: cacheDirectory)
        let header = root.appendingPathComponent("Sources/COSDI/ABI/osdi_0_4.h")
        guard try FixtureBuildCache.digest(header) == "0f3eeb9ef3c9f0bea9b3c59f34c632555e2367f6e28722e219d8c3f1d655a16f" else {
            throw FixtureBuildError.invalidInput("Pinned OSDI header identity mismatch")
        }
        let sources = root.appendingPathComponent("Tests/OSDIEvaluationTests/Fixtures")
        let run = FileManager.default.temporaryDirectory.appendingPathComponent("osdi-protocol-run-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: run, withIntermediateDirectories: false)
        var requests = names.enumerated().map { mode, name in
            let fileName = "protocol-\(name).dylib"
            let flags = compiler.arguments() + ["-dynamiclib", "-I", header.deletingLastPathComponent().path,
                "-DPROTOCOL_MODE=\(mode)", "-Wl,-install_name,@rpath/\(fileName)"]
            return FixtureBuildRequest(compiler: compiler, source: sources.appendingPathComponent("protocol.c"),
                dependencies: [header], arguments: flags, fileName: fileName)
        }
        let wire = root.appendingPathComponent("Sources/COSDI/include/OSDIPrecisionProtocol.h")
        requests.append(.init(compiler: compiler, source: sources.appendingPathComponent("precision_transport_worker.c"),
            dependencies: [wire], arguments: compiler.arguments() + ["-I", wire.deletingLastPathComponent().path],
            fileName: "precision-transport-worker", executable: true))
        let artifacts = try await cache.artifacts(requests)
        var compiled = 0
        var libraries: [String: EvaluationFixtureManifest.ProtocolLibrary] = [:]
        for (mode, name) in names.enumerated() {
            let artifact = artifacts[mode]
            if artifact.wasBuilt { compiled += 1 }
            let file = try artifact.materialize(to: run.appendingPathComponent("protocol-\(name).dylib"))
            libraries[name] = .init(path: file.path, sha256: artifact.sha256, mode: mode)
        }
        let artifact = artifacts[names.count]
        if artifact.wasBuilt { compiled += 1 }
        let worker = try artifact.materialize(to: run.appendingPathComponent("precision-transport-worker"))
        print("OSDI protocol fixtures: \(compiled) compiled, \(names.count + 1 - compiled) reused")
        return .init(schema: "osdi.standalone-fixtures.v1", protocolLibraries: libraries,
            transportWorker: .init(path: worker.path, sha256: artifact.sha256))
    }
}
