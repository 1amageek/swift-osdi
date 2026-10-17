import Foundation
import Darwin

actor EvaluationFixtureCache {
    static let shared = EvaluationFixtureCache()
    private var preparation: Task<EvaluationFixtureManifest, any Error>?

    func fixtures() async throws -> EvaluationFixtureManifest {
        if let preparation { return try await preparation.value }
        let task = Task { try await Self.buildFresh() }
        preparation = task
        return try await task.value
    }

    private static func buildFresh() async throws -> EvaluationFixtureManifest {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let compiler = try external("OSDI_OPENVAF_COMPILER", executable: true)
        let bsim = try external("OSDI_BSIM_LIBRARY", executable: false)
        guard let expectedDigest = ProcessInfo.processInfo.environment["OSDI_BSIM_SHA256"],
              !expectedDigest.isEmpty else { throw EvaluationFixtureError.requiredEnvironment("OSDI_BSIM_SHA256") }
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
        process.arguments = ["python3", root.appendingPathComponent("Tools/OSDIEvaluationVerification/build.py").path,
            "--output", directory.path, "--clang", "/usr/bin/clang",
            "--header", root.appendingPathComponent("Sources/COSDI/ABI/osdi_0_4.h").path,
            "--compiler", compiler, "--bsim", bsim, "--bsim-sha256", expectedDigest]
        if let receipt = ProcessInfo.processInfo.environment["OSDI_COMPILER_PROFILE_RECEIPT"], !receipt.isEmpty {
            process.arguments?.append(contentsOf: ["--compiler-profile-receipt", receipt])
        }
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
        return try JSONDecoder().decode(EvaluationFixtureManifest.self,
            from: Data(contentsOf: directory.appendingPathComponent("manifest.json")))
    }

    private static func external(_ key: String, executable: Bool) throws -> String {
        guard let path = ProcessInfo.processInfo.environment[key], !path.isEmpty,
              executable ? FileManager.default.isExecutableFile(atPath: path)
                         : FileManager.default.fileExists(atPath: path) else {
            throw EvaluationFixtureError.requiredEnvironment(key)
        }
        return path
    }
}
