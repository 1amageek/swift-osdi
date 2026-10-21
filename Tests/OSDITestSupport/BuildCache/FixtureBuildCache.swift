import Darwin
import Foundation
import SignoffToolSupport

public actor FixtureBuildCache {
    private struct Recipe: Codable, Equatable {
        let schema: Int
        let tools: [FixtureFileIdentity]
        let sdk: String
        let environment: [String: String]
        let inputs: [FixtureFileIdentity]
        let arguments: [String]
        let fileName: String
        let executable: Bool
    }
    private struct Receipt: Codable {
        let recipe: Recipe
        let key: String
        let sha256: String
    }

    private let directory: URL
    private let lockTimeout: Duration
    private let runner: any TimedProcessRunning

    public init(directory: URL, lockTimeout: Duration = .seconds(30),
                runner: any TimedProcessRunning = TimedProcessRunner(timeoutSeconds: 30,
                    maximumOutputByteCount: 1_048_576, cleanupTimeoutSeconds: 3)) {
        self.directory = directory
        self.lockTimeout = lockTimeout
        self.runner = runner
    }

    public static func digest(_ file: URL) throws -> String { try FixtureFileIdentity.digest(file) }

    public func artifacts(_ requests: [FixtureBuildRequest]) async throws -> [FixtureArtifact] {
        let width = min(4, max(1, ProcessInfo.processInfo.activeProcessorCount), requests.count)
        return try await withThrowingTaskGroup(of: (Int, Result<FixtureArtifact, any Error>).self) { group in
            var result = [FixtureArtifact?](repeating: nil, count: requests.count)
            var failures: [any Error] = []
            var next = 0
            func submit(_ index: Int) {
                let request = requests[index]
                group.addTask {
                    do { return (index, .success(try await self.artifact(request))) }
                    catch { return (index, .failure(error)) }
                }
            }
            for index in 0..<width { submit(index); next += 1 }
            for try await (index, outcome) in group {
                switch outcome {
                case .success(let artifact): result[index] = artifact
                case .failure(let error):
                    failures.append(error)
                    group.cancelAll()
                }
                if failures.isEmpty, next < requests.count { submit(next); next += 1 }
            }
            // Join every child and retain all cleanup owners before returning failure.
            if failures.count == 1 { throw failures[0] }
            if !failures.isEmpty { throw FixtureBuildError.batch(failures) }
            return result.map {
                guard let artifact = $0 else { preconditionFailure("Every submitted build must complete") }
                return artifact
            }
        }
    }

    public func artifact(_ request: FixtureBuildRequest) async throws -> FixtureArtifact {
        guard directory.isFileURL, lockTimeout > .zero,
              !request.fileName.isEmpty, request.fileName != ".", request.fileName != "..",
              !request.fileName.contains("/"), !request.arguments.contains("-o") else {
            throw FixtureBuildError.invalidInput("Invalid cache directory, deadline, output name or output override")
        }
        try Task.checkCancellation()
        let inputs = try ([request.source] + request.dependencies).map(FixtureFileIdentity.init)
        let recipe = Recipe(schema: 1, tools: request.compiler.identities, sdk: request.compiler.sdk.path,
            environment: request.compiler.environment, inputs: inputs, arguments: request.arguments,
            fileName: request.fileName, executable: request.executable)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let key = FixtureFileIdentity.digest(try encoder.encode(recipe))
        let locks = directory.appendingPathComponent("locks", isDirectory: true)
        let entries = directory.appendingPathComponent("entries", isDirectory: true)
        try FileManager.default.createDirectory(at: locks, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: entries, withIntermediateDirectories: true)
        let descriptor = locks.appendingPathComponent(key).path.withCString {
            open($0, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0o600)
        }
        guard descriptor >= 0 else { throw FixtureBuildError.lockFailed(errno) }
        // The scoped descriptor owns the OS lock across suspension; no descriptor escapes.
        defer { let result = close(descriptor); precondition(result == 0, "Owned lock descriptor close failed") }
        let deadline = ContinuousClock.now.advanced(by: lockTimeout)
        while flock(descriptor, LOCK_EX | LOCK_NB) != 0 {
            guard errno == EWOULDBLOCK else { throw FixtureBuildError.lockFailed(errno) }
            guard ContinuousClock.now < deadline else { throw FixtureBuildError.lockTimeout }
            try await Task.sleep(for: .milliseconds(10))
        }
        try Task.checkCancellation()
        let entry = entries.appendingPathComponent(key, isDirectory: true)
        let output = entry.appendingPathComponent(request.fileName)
        if FileManager.default.fileExists(atPath: entry.path) {
            do {
                let file = entry.appendingPathComponent("receipt.json")
                let size = try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                guard size > 0, size <= 1_048_576 else { throw FixtureBuildError.invalidInput("Invalid receipt size") }
                let receipt = try JSONDecoder().decode(Receipt.self, from: Data(contentsOf: file))
                guard receipt.key == key, receipt.recipe == recipe,
                      try FixtureFileIdentity.digest(output, maximumBytes: 16_777_216) == receipt.sha256 else {
                    throw FixtureBuildError.changedInput("Cached artifact/recipe mismatch")
                }
                return FixtureArtifact(file: output, sha256: receipt.sha256, wasBuilt: false, executable: request.executable)
            } catch {
                print("OSDI fixture cache invalidated \(request.fileName): \(error)")
                try FileManager.default.removeItem(at: entry)
            }
        }
        let pending = entries.appendingPathComponent("pending-\(key)-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: pending, withIntermediateDirectories: false)
        do {
            let built = pending.appendingPathComponent(request.fileName)
            let process = Process()
            process.executableURL = request.compiler.executable
            process.arguments = request.arguments + [request.source.path, "-o", built.path]
            process.environment = request.compiler.environment
            let result = try await runner.run(process: process, control: nil)
            guard result.exitCode == 0 else {
                throw FixtureBuildError.commandFailed(result)
            }
            try Task.checkCancellation()
            guard try ([request.source] + request.dependencies).map(FixtureFileIdentity.init) == inputs else {
                throw FixtureBuildError.changedInput("Source/header changed during compilation")
            }
            let sha = try FixtureFileIdentity.digest(built, maximumBytes: 16_777_216)
            try FileManager.default.setAttributes([.posixPermissions: request.executable ? 0o555 : 0o444], ofItemAtPath: built.path)
            try encoder.encode(Receipt(recipe: recipe, key: key, sha256: sha))
                .write(to: pending.appendingPathComponent("receipt.json"), options: .withoutOverwriting)
            guard rename(pending.path, entry.path) == 0 else { throw FixtureBuildError.publicationFailed(errno) }
            return FixtureArtifact(file: output, sha256: sha, wasBuilt: true, executable: request.executable)
        } catch {
            let primary = error
            if let process = error as? TimedProcessError,
               case .executionFailed(let failure) = process, failure.cleanupHandle != nil {
                throw FixtureBuildError.pendingCleanup(process: process, directory: pending)
            }
            do { try FileManager.default.removeItem(at: pending) }
            catch { throw FixtureBuildError.cleanup(primary: primary, secondary: error) }
            throw primary
        }
    }
}
