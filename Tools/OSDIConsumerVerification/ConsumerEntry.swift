import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts
import OSDIRuntime

@main
enum ConsumerEntry {
    static func main() async throws {
        guard CommandLine.arguments.count == 5 || CommandLine.arguments.count == 11 else { throw ConsumerFailure.arguments }
        let staging = URL(fileURLWithPath: CommandLine.arguments[3], isDirectory: true)
        guard let snapshot = try await exercise(URL(fileURLWithPath: CommandLine.arguments[1]), staging: staging, fatal: false) else {
            throw ConsumerFailure.invariant("Successful fixture did not publish a snapshot")
        }
        _ = try await exercise(URL(fileURLWithPath: CommandLine.arguments[2]), staging: staging, fatal: true)
        let output = URL(fileURLWithPath: CommandLine.arguments[4])
        let data = try JSONEncoder().encode(snapshot)
        try data.write(to: output, options: .withoutOverwriting)
        let restored = try JSONDecoder().decode(ConsumerSnapshot.self, from: Data(contentsOf: output))
        guard restored == snapshot else { throw ConsumerFailure.invariant("API JSON round trip") }
        if CommandLine.arguments.count == 11 {
            let observed = try await PrecisionConsumerProbe.exercise(
                libraryFile: URL(fileURLWithPath: CommandLine.arguments[5]),
                workerFile: URL(fileURLWithPath: CommandLine.arguments[6]),
                profile: ContentDigest(algorithm: .sha256, hexadecimalValue: CommandLine.arguments[7]),
                sourceDigest: ContentDigest(algorithm: .sha256, hexadecimalValue: CommandLine.arguments[8]),
                semanticProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: CommandLine.arguments[9]), staging: staging)
            let path = URL(fileURLWithPath: CommandLine.arguments[10])
            try JSONEncoder().encode(observed).write(to: path, options: .withoutOverwriting)
            guard try JSONDecoder().decode(PrecisionConsumerProbe.Snapshot.self, from: Data(contentsOf: path)) == observed else {
                throw ConsumerFailure.invariant("Precision JSON round trip")
            }
        }
        guard try FileManager.default.contentsOfDirectory(atPath: staging.path).isEmpty else {
            throw ConsumerFailure.invariant("Library staging retained after explicit close")
        }
    }

    static func load(_ file: URL, staging: URL) async throws -> OSDILibrary {
        let directory = file.deletingLastPathComponent()
        let reference = try LocalArtifactReferencer(digester: SHA256ContentDigester()).reference(
            ArtifactLocator(location: ArtifactLocation(workspaceRelativePath: file.lastPathComponent),
                role: .input, kind: .model, format: .raw), relativeTo: directory)
        let rootID = try ArtifactRootID(rawValue: "external-consumer")
        let root = try ArtifactRootCapability(rootID: rootID, directoryURL: directory,
            digester: SHA256ContentDigester())
        let result: Result<OSDILibrary, any Error>
        do {
            let budget = try ArtifactAccessBudget(maximumPageByteCount: 65_536,
                maximumTotalByteCount: 16_777_216, maximumPageCount: 4096,
                maximumWorkUnitCount: 8192, maximumDurationNanoseconds: 5_000_000_000)
            let intent = try ArtifactAccessIntent(expectedReference: reference,
                availability: .local(artifactID: reference.id, rootID: rootID,
                    relativePath: ArtifactRelativePath(segments: [file.lastPathComponent])),
                operation: .read, budget: budget)
            let limits = try OSDILoadLimits(maximumDescriptors: 16,
                maximumMetadataBytes: 16_777_216, maximumStorageBytes: 16_777_216)
            result = .success(try await OSDINativeLibraryLoader(stagingParentDirectory: staging)
                .load(intent, source: root, limits: limits))
        } catch { result = .failure(error) }
        do { try await root.close().wait() }
        catch {
            switch result {
            case .success(let library):
                do { try library.close() }
                catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
            case .failure(let primary): throw ConsumerFailure.cleanup(primary: primary, secondary: error)
            }
            throw error
        }
        return try result.get()
    }

    private static func exercise(_ file: URL, staging: URL, fatal: Bool) async throws -> ConsumerSnapshot? {
        let library = try await load(file, staging: staging)
        let snapshot: ConsumerSnapshot?
        do { snapshot = try probe(library, fatal: fatal) }
        catch {
            do { try library.close() }
            catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
            throw error
        }
        try library.close()
        return snapshot
    }

    private static func probe(_ library: borrowing OSDILibrary, fatal: Bool) throws -> ConsumerSnapshot? {
        guard library.descriptors.count == 1, library.descriptors[0].name == "protocol_transport" else {
            throw ConsumerFailure.invariant("Unexpected protocol descriptor")
        }
        let limits = try OSDIEvaluationLimits(maximumWorkspaceBytes: 8_388_608,
            maximumDiagnosticCount: 256, maximumDiagnosticBytes: 65_536, maximumStringBytes: 65_536)
        let options = try OSDIPreparationOptions(parameters: [], simulationParameters: [],
            temperatureKelvin: 300.15, connectedTerminalCount: 1, limits: limits)
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        defer { model.close() }
        guard model.topology.activeUnknownCount == 1, model.topology.stateCount == 2 else {
            throw ConsumerFailure.invariant("Unexpected prepared topology")
        }
        do {
            try library.close()
            throw ConsumerFailure.invariant("Library closed with an active prepared owner")
        } catch let error as OSDIError {
            guard case .libraryInUse = error else { throw error }
        }
        let values = [3.0], previous = [2.0, 7.0]
        let evaluator = OSDINativeModelEvaluator()
        let context = try OSDIEvaluationContext(mode: .transient, timeSeconds: 4)
        if fatal {
            var called = false
            do {
                try evaluator.evaluate(model: &model, context: context, unknowns: values.span,
                    previousState: previous.span) { _ in called = true }
                throw ConsumerFailure.invariant("Fatal evaluation succeeded")
            } catch let error as OSDIError {
                guard case .native(let issue) = error, issue.code == 17, issue.modelFlags == 2 else { throw error }
            }
            guard !called else { throw ConsumerFailure.invariant("Fatal evaluation published borrowed output") }
            return nil
        }
        let first = try evaluator.evaluate(model: &model, context: context, unknowns: values.span,
            previousState: previous.span) { ConsumerSnapshot.capture($0) }
        let expected = ConsumerSnapshot(f: [5], q: [6], g: [1], c: [0], nextState: [5, 7])
        guard first == expected else { throw ConsumerFailure.invariant("Unexpected F/Q/G/C or next state") }
        let repeated = try evaluator.evaluate(model: &model, context: context, unknowns: values.span,
            previousState: previous.span) { ConsumerSnapshot.capture($0) }
        guard repeated == first else { throw ConsumerFailure.invariant("Implicit state acceptance") }
        let wrong: [Double] = []
        do {
            try evaluator.evaluate(model: &model, unknowns: wrong.span, previousState: previous.span) { _ in }
            throw ConsumerFailure.invariant("Wrong dimensions succeeded")
        } catch let error as OSDIError {
            guard case .invalidRange = error else { throw error }
        }
        model.close()
        model.close()
        do {
            try evaluator.evaluate(model: &model, unknowns: values.span, previousState: previous.span) { _ in }
            throw ConsumerFailure.invariant("Closed model evaluated")
        } catch let error as OSDIError {
            guard case .closed = error else { throw error }
        }
        return first
    }
}
