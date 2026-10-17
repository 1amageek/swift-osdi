import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import Foundation
import OSDIContracts
import OSDIRuntime

@main
enum OSDIInspectEntry {
    private enum Failure: Error { case arguments, schema, storage }

    static func main() async {
        if CommandLine.arguments == [CommandLine.arguments[0], "--help"] {
            print("Usage: osdi-inspect --request REQUEST.json --output REPORT.json")
            return
        }
        var output: URL?
        var request: OSDIInspectRequest?
        do {
            let args = CommandLine.arguments
            guard args.count == 5, args[1] == "--request", args[3] == "--output" else { throw Failure.arguments }
            output = URL(fileURLWithPath: args[4])
            let decoded = try JSONDecoder().decode(OSDIInspectRequest.self,
                from: Data(contentsOf: URL(fileURLWithPath: args[2])))
            request = decoded
            guard decoded.schemaVersion == 1 else { throw Failure.schema }
            let report = try await execute(decoded)
            try save(report, to: output!)
            print("OSDI admission passed: \(report.descriptors?.count ?? 0) descriptors; no model evaluation")
        } catch {
            let classification = classify(error)
            let report = OSDIInspectReport(succeeded: false,
                expectedSHA256: request?.expectedSHA256, expectedByteCount: request?.expectedByteCount,
                descriptorStride: nil, descriptors: nil, storageVerified: nil,
                reason: classification.0, detail: String(describing: error), nativeStatus: classification.1)
            do {
                if let output { try save(report, to: output) }
                else { FileHandle.standardError.write(try JSONEncoder().encode(report)) }
            } catch {
                FileHandle.standardError.write(Data("OSDI report write failed: \(error)\n".utf8))
            }
            exit(1)
        }
    }

    private static func execute(_ request: OSDIInspectRequest) async throws -> OSDIInspectReport {
        let file = URL(fileURLWithPath: request.libraryPath).standardizedFileURL
        let digest = try ContentDigest(algorithm: .sha256, hexadecimalValue: request.expectedSHA256)
        let reference = try ArtifactReference(digest: digest, byteCount: request.expectedByteCount,
            descriptor: .init(role: .input, kind: .model, format: try .init(rawValue: "osdi")))
        let rootID = try ArtifactRootID(rawValue: "osdi-inspect")
        let source = try ArtifactRootCapability(rootID: rootID,
            directoryURL: file.deletingLastPathComponent(), digester: SHA256ContentDigester())
        var failure: OSDIError?
        var report: OSDIInspectReport?
        do {
            let availability = ArtifactAvailability.local(artifactID: reference.id, rootID: rootID,
                relativePath: try ArtifactRelativePath(segments: [file.lastPathComponent]))
            let b = request.readBudget
            let budget = try ArtifactAccessBudget(maximumPageByteCount: b.maximumPageByteCount,
                maximumTotalByteCount: b.maximumTotalByteCount, maximumPageCount: b.maximumPageCount,
                maximumWorkUnitCount: b.maximumWorkUnitCount, maximumDurationNanoseconds: b.maximumDurationNanoseconds)
            let intent = try ArtifactAccessIntent(expectedReference: reference,
                availability: availability, operation: .read, budget: budget)
            let l = request.limits
            let limits = try OSDILoadLimits(maximumDescriptors: l.maximumDescriptors,
                maximumMetadataBytes: l.maximumMetadataBytes, maximumStorageBytes: l.maximumStorageBytes)
            let library = try await OSDINativeLibraryLoader().load(intent, source: source, limits: limits)
            var libraryFailure: OSDIError?
            do {
                if request.verifyStorage {
                    for descriptor in library.descriptors {
                        var storage = try library.makeStorage(descriptorIndex: descriptor.index)
                        let modelZero = storage.withModelBytes { bytes in
                            for index in 0..<bytes.count { if bytes[index] != 0 { return false } }
                            return true
                        }
                        let instanceZero = storage.withInstanceBytes { bytes in
                            for index in 0..<bytes.count { if bytes[index] != 0 { return false } }
                            return true
                        }
                        guard modelZero && instanceZero else { throw Failure.storage }
                    }
                }
                report = OSDIInspectReport(succeeded: true, expectedSHA256: reference.digest.hexadecimalValue,
                    expectedByteCount: reference.byteCount, descriptorStride: library.descriptorStride,
                    descriptors: library.descriptors.map(OSDIInspectReport.Descriptor.init),
                    storageVerified: request.verifyStorage, reason: nil, detail: nil, nativeStatus: nil)
            } catch {
                libraryFailure = (error as? OSDIError) ?? .host(operation: "inspectStorage", underlying: error)
            }
            do { try library.close() }
            catch {
                libraryFailure = libraryFailure.map { .cleanup(primary: $0, secondary: error) } ?? error
            }
            if let libraryFailure { throw libraryFailure }
        } catch {
            failure = (error as? OSDIError) ?? .host(operation: "inspect", underlying: error)
        }
        let termination = await source.close()
        do { try await termination.wait() }
        catch {
            let closeFailure = OSDIError.host(operation: "closeSource", underlying: error)
            failure = failure.map { .cleanup(primary: $0, secondary: closeFailure) } ?? closeFailure
        }
        if let failure { throw failure }
        guard let report else { throw Failure.storage }
        return report
    }

    private static func save(_ report: OSDIInspectReport, to path: URL) throws {
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(report).write(to: path, options: .atomic)
    }

    private static func classify(_ error: any Error) -> (String, Int32?) {
        guard let error = error as? OSDIError else { return ("invalid_request", nil) }
        switch error {
        case .native(let issue): return ("native_admission", issue.code)
        case .artifact: return ("artifact_access", nil)
        case .artifactClose: return ("artifact_close", nil)
        case .identityMismatch: return ("identity_mismatch", nil)
        case .cancelled: return ("cancelled", nil)
        case .resourceLimit: return ("resource_limit", nil)
        case .libraryInUse: return ("library_in_use", nil)
        case .closed: return ("library_closed", nil)
        case .cleanup, .pendingCleanup, .cleanupRequired: return ("cleanup_failure", nil)
        default: return ("runtime_failure", nil)
        }
    }
}
