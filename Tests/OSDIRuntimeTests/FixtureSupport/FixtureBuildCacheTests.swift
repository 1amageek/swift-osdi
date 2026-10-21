import Darwin
import Foundation
import OSDITestSupport
import SignoffToolSupport
import Testing

@Suite(.timeLimit(.minutes(1)))
struct FixtureBuildCacheTests {
    private actor CompilerProbe: TimedProcessRunning {
        private(set) var invocations = 0
        var failing = false
        var delay: Duration = .zero

        func configure(failing: Bool = false, delay: Duration = .zero) {
            self.failing = failing
            self.delay = delay
        }

        func run(process: Process, control: TimedProcessControl?) async throws -> TimedProcessResult {
            invocations += 1
            if delay > .zero { try await Task.sleep(for: delay) }
            if failing { return .init(exitCode: 19, standardOutput: "", standardError: "compile rejected") }
            let args = try #require(process.arguments)
            let source = URL(fileURLWithPath: args[args.count - 3])
            let output = URL(fileURLWithPath: args[args.count - 1])
            try Data(contentsOf: source).write(to: output)
            return .init(exitCode: 0, standardOutput: "", standardError: "")
        }
    }

    @Test
    func reuseInvalidationConcurrentPublicationAndFailure() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("osdi-cache-test-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: false)
        let source = root.appendingPathComponent("input.c")
        let other = root.appendingPathComponent("other.c")
        let header = root.appendingPathComponent("input.h")
        try Data("first".utf8).write(to: source)
        try Data("independent".utf8).write(to: other)
        try Data("header-v1".utf8).write(to: header)
        let compiler = try await FixtureCompiler.selected()
        let probe = CompilerProbe()
        let directory = root.appendingPathComponent("cache")
        let a = FixtureBuildCache(directory: directory, runner: probe)
        let b = FixtureBuildCache(directory: directory, runner: probe)
        func request(_ source: URL, headers: [URL] = [], flag: String = "v1") -> FixtureBuildRequest {
            .init(compiler: compiler, source: source, dependencies: headers, arguments: [flag], fileName: "fixture")
        }
        let dependent = request(source, headers: [header])
        let independent = request(other)
        let first = try await a.artifact(dependent)
        #expect(first.wasBuilt)
        #expect(try Data(contentsOf: first.file) == Data("first".utf8))
        let firstReceipt = try Data(contentsOf: first.file.deletingLastPathComponent().appendingPathComponent("receipt.json"))
        let hit = try await b.artifact(dependent)
        #expect(!hit.wasBuilt)
        #expect(try Data(contentsOf: hit.file.deletingLastPathComponent().appendingPathComponent("receipt.json")) == firstReceipt)
        #expect(await probe.invocations == 1)
        _ = try await a.artifact(independent)
        try Data("header-v2".utf8).write(to: header)
        let changed = try await a.artifact(dependent)
        #expect(changed.wasBuilt && changed.file != first.file)
        #expect(!(try await b.artifact(independent)).wasBuilt)
        #expect(await probe.invocations == 3)
        let flagChanged = try await a.artifact(request(source, headers: [header], flag: "v2"))
        #expect(flagChanged.wasBuilt && flagChanged.file != changed.file)
        try Data("damaged".utf8).write(to: changed.file, options: .atomic)
        let repaired = try await b.artifact(dependent)
        #expect(repaired.wasBuilt)
        #expect(try Data(contentsOf: repaired.file) == Data("first".utf8))
        let receipt = repaired.file.deletingLastPathComponent().appendingPathComponent("receipt.json")
        try Data("not-json".utf8).write(to: receipt, options: .atomic)
        #expect((try await a.artifact(dependent)).wasBuilt)
        let count = await probe.invocations
        await probe.configure(delay: .milliseconds(50))
        let concurrent = request(source, flag: "concurrent")
        async let one = a.artifact(concurrent)
        async let two = b.artifact(concurrent)
        let both = try await [one, two]
        #expect(both.filter(\.wasBuilt).count == 1)
        #expect(await probe.invocations == count + 1)
        #expect(both[0].sha256 == both[1].sha256)
        await probe.configure(delay: .milliseconds(100))
        let locked = request(source, flag: "locked")
        let beforeLock = await probe.invocations
        let holder = Task { try await a.artifact(locked) }
        let lockDeadline = ContinuousClock.now.advanced(by: .seconds(5))
        while await probe.invocations == beforeLock {
            guard ContinuousClock.now < lockDeadline else { throw FixtureBuildError.lockTimeout }
            try await Task.sleep(for: .milliseconds(5))
        }
        let shortWait = FixtureBuildCache(directory: directory, lockTimeout: .milliseconds(10), runner: probe)
        do {
            _ = try await shortWait.artifact(locked)
            Issue.record("Same-key waiter ignored its lock deadline")
        } catch let error as FixtureBuildError {
            guard case .lockTimeout = error else { throw error }
        }
        _ = try await holder.value

        await probe.configure(failing: true)
        let failedDirectory = root.appendingPathComponent("failed-cache")
        let failed = FixtureBuildCache(directory: failedDirectory, runner: probe)
        do {
            _ = try await failed.artifact(dependent)
            Issue.record("Failed compilation published a ready artifact")
        } catch let error as FixtureBuildError {
            guard case .commandFailed(let result) = error else { throw error }
            #expect(result.exitCode == 19 && result.standardError == "compile rejected")
        }
        #expect(try FileManager.default.contentsOfDirectory(atPath: failedDirectory.appendingPathComponent("entries").path).isEmpty)
        let batchDirectory = root.appendingPathComponent("failed-batch")
        let batch = FixtureBuildCache(directory: batchDirectory, runner: probe)
        do {
            _ = try await batch.artifacts([request(source, flag: "batch-a"), request(source, flag: "batch-b")])
            Issue.record("Failed batch published successful results")
        } catch let error as FixtureBuildError {
            switch error {
            case .batch(let errors): #expect(!errors.isEmpty)
            case .commandFailed(let result): #expect(result.exitCode == 19)
            default: throw error
            }
        }
        #expect(try FileManager.default.contentsOfDirectory(atPath: batchDirectory.appendingPathComponent("entries").path).isEmpty)
        await probe.configure()
        #expect((try await failed.artifact(dependent)).wasBuilt)

        await probe.configure(delay: .seconds(5))
        let cancelledDirectory = root.appendingPathComponent("cancelled-cache")
        let cancelled = FixtureBuildCache(directory: cancelledDirectory, runner: probe)
        let beforeCancel = await probe.invocations
        let task = Task { try await cancelled.artifact(dependent) }
        let deadline = ContinuousClock.now.advanced(by: .seconds(5))
        while await probe.invocations == beforeCancel {
            guard ContinuousClock.now < deadline else { throw FixtureBuildError.lockTimeout }
            try await Task.sleep(for: .milliseconds(5))
        }
        task.cancel()
        do {
            _ = try await task.value
            Issue.record("Cancelled build published an artifact")
        } catch is CancellationError {}
        #expect(try FileManager.default.contentsOfDirectory(atPath: cancelledDirectory.appendingPathComponent("entries").path).isEmpty)
        await probe.configure()
        try Data("second".utf8).write(to: source)
        let sourceChanged = try await a.artifact(dependent)
        #expect(sourceChanged.wasBuilt && sourceChanged.file != first.file)
        #expect(try Data(contentsOf: sourceChanged.file) == Data("second".utf8))
    }
    @Test
    func actualProcessTimeoutAndCancellationPreserveCleanup() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("osdi-cache-process-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: false)
        let source = root.appendingPathComponent("input.c")
        try Data("input".utf8).write(to: source)
        let compilerScript = root.appendingPathComponent("controlled-compiler")
        let pid = root.appendingPathComponent("child.pid")
        func quote(_ text: String) -> String { "'" + text.replacingOccurrences(of: "'", with: "'\"'\"'") + "'" }
        let script = "#!/bin/sh\nsleep 10 &\necho $! > " + quote(pid.path) + "\nwait\n"
        try Data(script.utf8).write(to: compilerScript)
        try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: compilerScript.path)
        let compiler = try await FixtureCompiler.selected(executable: compilerScript)
        let runner = TimedProcessRunner(timeoutSeconds: 0.3, terminationGraceSeconds: 0.05,
            pipeDrainGraceSeconds: 0.05, maximumOutputByteCount: 4096, cleanupTimeoutSeconds: 1)
        for cancel in [false, true] {
            if FileManager.default.fileExists(atPath: pid.path) { try FileManager.default.removeItem(at: pid) }
            let directory = root.appendingPathComponent(cancel ? "cancel" : "timeout")
            let cache = FixtureBuildCache(directory: directory, runner: runner)
            let request = FixtureBuildRequest(compiler: compiler, source: source, dependencies: [],
                arguments: [], fileName: "artifact")
            let task = Task { try await cache.artifact(request) }
            let deadline = ContinuousClock.now.advanced(by: .seconds(5))
            while !FileManager.default.fileExists(atPath: pid.path) {
                guard ContinuousClock.now < deadline else { throw FixtureBuildError.lockTimeout }
                try await Task.sleep(for: .milliseconds(5))
            }
            if cancel { task.cancel() }
            do {
                _ = try await task.value
                Issue.record("Incomplete process published a ready artifact")
            } catch let error as TimedProcessError {
                guard case .executionFailed(let failure) = error else { throw error }
                #expect(failure.cause == (cancel ? .cancelled : .timedOut(timeoutSeconds: 0.3)))
                #expect(failure.cleanupHandle == nil)
            }
            #expect(try FileManager.default.contentsOfDirectory(atPath: directory.appendingPathComponent("entries").path).isEmpty)
            let text = try String(contentsOf: pid, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)
            let child = try #require(Int32(text))
            #expect(kill(child, 0) != 0 && errno == ESRCH)
        }
    }

}
