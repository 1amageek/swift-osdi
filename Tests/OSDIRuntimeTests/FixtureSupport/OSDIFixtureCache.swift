import Foundation
#if canImport(Darwin)
import Darwin
#endif

/// Serializes and caches successful compilation for every runtime test suite.
actor OSDIFixtureCache {
    static let shared = OSDIFixtureCache()

    private var preparation: Task<OSDIFixtureSet, any Error>?

    func fixtures() async throws -> OSDIFixtureSet {
        if let preparation { return try await preparation.value }
        let task = Task { try await Self.buildFresh() }
        preparation = task
        return try await task.value
    }

    private static func buildFresh() async throws -> OSDIFixtureSet {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let output = FileManager.default.temporaryDirectory
            .appendingPathComponent("swift-osdi-fixtures-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        var compilerArguments: [String] = []
        if ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1" {
            guard let compilerPath = ProcessInfo.processInfo.environment["OSDI_OPENVAF_COMPILER"],
                  FileManager.default.isExecutableFile(atPath: compilerPath) else {
                throw OSDIFixtureError.missingOpenVAF("OSDI_OPENVAF_COMPILER must select an executable for requested model qualification")
            }
            compilerArguments = ["--openvaf", compilerPath]
        }
        let log = output.appendingPathComponent("builder.log")
        guard FileManager.default.createFile(atPath: log.path, contents: nil) else {
            throw OSDIFixtureError.cannotCreateLog(log.path)
        }
        let handle = try FileHandle(forWritingTo: log)
        defer { handle.closeFile() }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = [
            "python3", root.appendingPathComponent("Tools/OSDIVerification/build.py").path,
            "--output", output.path, "--clang", ProcessInfo.processInfo.environment["OSDI_TEST_CLANG"] ?? "/usr/bin/clang",
            "--header", root.appendingPathComponent("Sources/COSDI/ABI/osdi_0_4.h").path
        ] + compilerArguments
        process.standardOutput = handle
        process.standardError = handle
        try process.run()
        let deadline = ContinuousClock.now.advanced(by: .seconds(25))
        do {
            while process.isRunning {
                guard ContinuousClock.now < deadline else { throw OSDIFixtureError.timeout }
                try await Task.sleep(for: .milliseconds(20))
            }
        } catch {
            process.terminate()
            // An independent task preserves the builder's SIGTERM cleanup window
            // even when the requesting task is already canceled.
            await Task.detached {
                let shutdown = ContinuousClock.now.advanced(by: .seconds(1))
                while process.isRunning && ContinuousClock.now < shutdown {
                    do { try await Task.sleep(for: .milliseconds(10)) }
                    catch { return }
                }
            }.value
            #if canImport(Darwin)
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            #endif
            throw error
        }
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            let message = String(decoding: try Data(contentsOf: log), as: UTF8.self)
            throw OSDIFixtureError.buildFailed(process.terminationStatus, message)
        }
        let data = try Data(contentsOf: output.appendingPathComponent("manifest.json"))
        let manifest = try JSONDecoder().decode(OSDIFixtureManifest.self, from: data)
        return OSDIFixtureSet(directory: output, manifest: manifest)
    }
}
