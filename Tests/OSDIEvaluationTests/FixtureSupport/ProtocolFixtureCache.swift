import Foundation
import Darwin

actor ProtocolFixtureCache {
    static let shared = ProtocolFixtureCache()
    private var preparation: Task<ProtocolFixtureManifest, any Error>?

    func fixtures() async throws -> ProtocolFixtureManifest {
        if let preparation { return try await preparation.value }
        let task = Task { try await Self.buildFresh() }
        preparation = task
        return try await task.value
    }

    private static func buildFresh() async throws -> ProtocolFixtureManifest {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        if let path = ProcessInfo.processInfo.environment["OSDI_STANDALONE_FIXTURES"] {
            return try decode(URL(fileURLWithPath: path))
        }
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("osdi-S2-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let log = directory.appendingPathComponent("builder.log")
        guard FileManager.default.createFile(atPath: log.path, contents: nil) else {
            throw EvaluationFixtureError.invalidReference("cannot create builder log")
        }
        let handle = try FileHandle(forWritingTo: log)
        defer { handle.closeFile() }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = ["python3", root.appendingPathComponent("Tools/OSDIStandaloneVerification/prepare.py").path,
            "--output", directory.path, "--clang", ProcessInfo.processInfo.environment["OSDI_TEST_CLANG"] ?? "/usr/bin/clang"]
        process.standardOutput = handle
        process.standardError = handle
        try process.run()
        let deadline = ContinuousClock.now.advanced(by: .seconds(25))
        do {
            while process.isRunning {
                guard ContinuousClock.now < deadline else { throw EvaluationFixtureError.timeout }
                try await Task.sleep(for: .milliseconds(20))
            }
        } catch {
            process.terminate()
            await Task.detached {
                let deadline = ContinuousClock.now.advanced(by: .seconds(1))
                while process.isRunning && ContinuousClock.now < deadline {
                    do { try await Task.sleep(for: .milliseconds(10)) }
                    catch { return }
                }
            }.value
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            throw error
        }
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw EvaluationFixtureError.buildFailed(process.terminationStatus,
                String(decoding: try Data(contentsOf: log), as: UTF8.self))
        }
        return try decode(directory.appendingPathComponent("manifest.json"))
    }

    private static func decode(_ path: URL) throws -> ProtocolFixtureManifest {
        let manifest = try JSONDecoder().decode(ProtocolFixtureManifest.self, from: Data(contentsOf: path))
        guard manifest.schema == "osdi.standalone-fixtures.v1", manifest.protocolLibraries.count == 15 else {
            throw EvaluationFixtureError.invalidReference("invalid standalone fixture manifest")
        }
        return manifest
    }
}
