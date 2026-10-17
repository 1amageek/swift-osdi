import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts
import OSDIRuntime

struct EvaluationArtifact {
    let intent: ArtifactAccessIntent
    let source: ArtifactRootCapability
    let loader: OSDINativeLibraryLoader

    init(_ library: String) throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("osdi-S2-input-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try FileManager.default.copyItem(atPath: library, toPath: directory.appendingPathComponent("input.osdi").path)
        let reference = try LocalArtifactReferencer(digester: SHA256ContentDigester()).reference(
            ArtifactLocator(location: ArtifactLocation(workspaceRelativePath: "input.osdi"),
                            role: .input, kind: .model, format: .raw), relativeTo: directory)
        let rootID = try ArtifactRootID(rawValue: UUID().uuidString)
        source = try ArtifactRootCapability(rootID: rootID, directoryURL: directory, digester: SHA256ContentDigester())
        intent = try ArtifactAccessIntent(expectedReference: reference,
            availability: .local(artifactID: reference.id, rootID: rootID,
                                 relativePath: ArtifactRelativePath(segments: ["input.osdi"])),
            operation: .read,
            budget: ArtifactAccessBudget(maximumPageByteCount: 65_536,
                maximumTotalByteCount: 16_777_216, maximumPageCount: 4096,
                maximumWorkUnitCount: 8192, maximumDurationNanoseconds: 5_000_000_000))
        loader = OSDINativeLibraryLoader(stagingParentDirectory: directory)
    }

    func load() async throws -> OSDILibrary {
        let limits = try OSDILoadLimits(maximumDescriptors: 16,
            maximumMetadataBytes: 16_777_216, maximumStorageBytes: 16_777_216)
        let result: Result<OSDILibrary, OSDIError>
        do { result = .success(try await loader.load(intent, source: source, limits: limits)) }
        catch { result = .failure(error) }
        do { try await source.close().wait() }
        catch {
            if case .failure(let primary) = result { throw EvaluationFixtureError.cleanup(primary, error) }
            throw error
        }
        return try result.get()
    }
}
