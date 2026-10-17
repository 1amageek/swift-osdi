import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import Foundation
import OSDIContracts
import OSDIRuntime

@main
enum OSDIEvaluateEntry {
    static func main() async {
        if CommandLine.arguments == [CommandLine.arguments[0], "--help"] {
            print("Usage: osdi-evaluate --request REQUEST.json --output REPORT.json")
            return
        }
        var output: URL?
        var request: OSDIEvaluateRequest?
        var binding: OSDIEvaluateReport.Binding?
        do {
            let args = CommandLine.arguments
            guard args.count == 5, args[1] == "--request", args[3] == "--output" else {
                throw OSDIEvaluateFailure.invalidRequest("Expected --request PATH --output PATH")
            }
            output = URL(fileURLWithPath: args[4])
            let bytes = try Data(contentsOf: URL(fileURLWithPath: args[2]))
            binding = try requestBinding(bytes)
            let decoded = try JSONDecoder().decode(OSDIEvaluateRequest.self, from: bytes)
            request = decoded
            let report = try await execute(decoded, binding: binding!)
            try save(report, to: output!)
            if decoded.operation == "prepare" {
                print("OSDI preparation passed; topology and parameter facts only")
            } else {
                print("OSDI point evaluation passed; raw F/Q/G/C only, no circuit solution")
            }
        } catch {
            let report = OSDIEvaluateReport(succeeded: false, requestBinding: binding,
                input: request, failure: classify(error))
            do {
                if let output { try save(report, to: output) }
                else { FileHandle.standardError.write(try JSONEncoder().encode(report)) }
            } catch {
                FileHandle.standardError.write(Data("OSDI report write failed: \(error)\n".utf8))
            }
            exit(1)
        }
    }

    private static func requestBinding(_ data: Data) throws -> OSDIEvaluateReport.Binding {
        // The request is copied once at the external JSON/hash boundary, outside model evaluation.
        let bytes = Array(data)
        let count = UInt64(bytes.count)
        let limits = try ContentDigestSessionLimits(maximumChunkByteCount: max(count, 1),
            maximumTotalByteCount: max(count, 1), maximumUpdateCount: 1)
        let result = try SHA256ContentDigester().digest(using: .sha256, limits: limits) { (lease: borrowing ContentDigestUpdateLease) throws(ContentDigestError) in
            try lease.update(bytes)
        }
        return .init(sha256: result.digest.hexadecimalValue, byteCount: result.totalByteCount)
    }

    private static func execute(_ request: OSDIEvaluateRequest,
                                binding: OSDIEvaluateReport.Binding) async throws -> OSDIEvaluateReport {
        let options = try request.preparationOptions()
        let file = URL(fileURLWithPath: request.libraryPath).standardizedFileURL
        let reference = try ArtifactReference(
            digest: ContentDigest(algorithm: .sha256, hexadecimalValue: request.expectedSHA256),
            byteCount: request.expectedByteCount,
            descriptor: .init(role: .input, kind: .model, format: try .init(rawValue: "osdi")))
        let rootID = try ArtifactRootID(rawValue: "osdi-evaluate")
        let source = try ArtifactRootCapability(rootID: rootID,
            directoryURL: file.deletingLastPathComponent(), digester: SHA256ContentDigester())
        var failure: (any Error)?
        var report: OSDIEvaluateReport?
        do {
            let availability = ArtifactAvailability.local(artifactID: reference.id, rootID: rootID,
                relativePath: try ArtifactRelativePath(segments: [file.lastPathComponent]))
            let b = request.readBudget
            let budget = try ArtifactAccessBudget(maximumPageByteCount: b.maximumPageByteCount,
                maximumTotalByteCount: b.maximumTotalByteCount, maximumPageCount: b.maximumPageCount,
                maximumWorkUnitCount: b.maximumWorkUnitCount,
                maximumDurationNanoseconds: b.maximumDurationNanoseconds)
            let intent = try ArtifactAccessIntent(expectedReference: reference,
                availability: availability, operation: .read, budget: budget)
            let l = request.limits
            let limits = try OSDILoadLimits(maximumDescriptors: l.maximumDescriptors,
                maximumMetadataBytes: l.maximumMetadataBytes, maximumStorageBytes: l.maximumStorageBytes)
            let library = try await OSDINativeLibraryLoader().load(intent, source: source, limits: limits)
            do {
                report = try evaluate(request, binding: binding, library: library, options: options)
            } catch { failure = error }
            do { try library.close() }
            catch { failure = combine(failure, cleanup: error) }
        } catch { failure = error }
        let termination = await source.close()
        do { try await termination.wait() }
        catch { failure = combine(failure, cleanup: .host(operation: "closeSource", underlying: error)) }
        if let failure { throw failure }
        guard let report else { throw OSDIEvaluateFailure.invalidRequest("Evaluation produced no report") }
        return report
    }

    private static func evaluate(_ request: OSDIEvaluateRequest,
                                 binding: OSDIEvaluateReport.Binding,
                                 library: borrowing OSDILibrary,
                                 options: borrowing OSDIPreparationOptions) throws -> OSDIEvaluateReport {
        guard library.descriptors.indices.contains(request.descriptorIndex) else {
            throw OSDIEvaluateFailure.invalidRequest("Descriptor index is out of range")
        }
        var model = try OSDINativeModelPreparer().prepare(library: library,
            descriptorIndex: request.descriptorIndex, options: options)
        defer { model.close() }
        let prepared = OSDIEvaluateReport.Prepared(noiseSources: request.schemaVersion == 2 ? model.noiseSources.map(OSDIEvaluateReport.NoiseSource.init) : nil, topology: .init(model.topology),
            parameterMetadata: model.parameterMetadata.map(OSDIEvaluateReport.ParameterInfo.init),
            resolvedParameters: model.resolvedParameters.map(OSDIEvaluateReport.ResolvedParameter.init),
            diagnostics: model.diagnostics.map(OSDIEvaluateReport.Diagnostic.init))
        let descriptor = OSDIEvaluateReport.Descriptor(library.descriptors[request.descriptorIndex],
            stride: library.descriptorStride)
        if request.operation == "prepare" {
            return OSDIEvaluateReport(succeeded: true, requestBinding: binding, input: request,
                descriptor: descriptor, prepared: prepared)
        }
        guard let localUnknowns = request.localUnknowns, let previousState = request.previousState else {
            throw OSDIEvaluateFailure.invalidUnknowns("Evaluate requires explicit localUnknowns and previousState arrays")
        }
        guard localUnknowns.count == model.topology.activeUnknownCount,
              localUnknowns.allSatisfy({ $0.isFinite }),
              previousState.count == model.topology.stateCount,
              previousState.allSatisfy({ $0.isFinite }) else {
            throw OSDIEvaluateFailure.invalidUnknowns("Expected \(model.topology.activeUnknownCount) active unknowns and \(model.topology.stateCount) previous-state values; received \(localUnknowns.count) and \(previousState.count). Every value must be finite and follow prepared order.")
        }
        let evaluated: Result<OSDIEvaluateReport.Evaluation, OSDIError> = try OSDINativeModelEvaluator().evaluate(
            model: &model, context: try request.evaluationContext(), unknowns: localUnknowns.span,
            previousState: previousState.span) { view in
                do throws(OSDIError) {
                    var diagnostics: [OSDIEvaluateReport.Diagnostic] = []
                    diagnostics.reserveCapacity(view.diagnosticCount)
                    for index in 0..<view.diagnosticCount {
                        diagnostics.append(.init(try view.diagnostic(at: index)))
                    }
                    return .success(.init(f: materialize(view.f), q: materialize(view.q), g: materialize(view.g),
                        c: materialize(view.c), nextState: materialize(view.nextState),
                        noiseDensities: request.schemaVersion == 2 ? materialize(view.noiseDensities) : nil,
                        maximumTimestep: request.schemaVersion == 2 ? view.maximumTimestep : nil,
                        modelFlags: view.modelFlags, diagnostics: diagnostics))
                } catch { return .failure(error) }
            }
        return OSDIEvaluateReport(succeeded: true, requestBinding: binding, input: request,
            descriptor: descriptor,
            prepared: prepared, evaluation: try evaluated.get())
    }

    private static func materialize(_ span: borrowing Span<Double>) -> [Double] {
        // Owned JSON output is the serialization boundary for the borrowed native result.
        var result: [Double] = []
        result.reserveCapacity(span.count)
        for index in 0..<span.count { result.append(span[index]) }
        return result
    }

    private static func combine(_ primary: (any Error)?, cleanup: OSDIError) -> OSDIError {
        guard let primary else { return cleanup }
        return .cleanup(primary: (primary as? OSDIError) ?? .host(operation: "evaluate", underlying: primary),
            secondary: cleanup)
    }

    private static func save(_ report: OSDIEvaluateReport, to path: URL) throws {
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(report)
        try data.write(to: path, options: .atomic)
        let reread = try Data(contentsOf: path)
        guard reread == data else { throw OSDIEvaluateFailure.reportRoundTrip }
        _ = try JSONDecoder().decode(OSDIEvaluateReport.self, from: reread)
    }

    private static func classify(_ error: any Error) -> OSDIEvaluateReport.Failure {
        var reason = "runtime_failure"
        var native: OSDINativeIssue?
        if let error = error as? OSDIEvaluateFailure {
            switch error {
            case .invalidRequest: reason = "invalid_request"
            case .invalidParameter: reason = "invalid_parameter"
            case .invalidTemperature: reason = "invalid_temperature"
            case .invalidUnknowns: reason = "invalid_unknowns"
            case .unsupportedCapability: reason = "unsupported_capability"
            case .reportRoundTrip: reason = "report_round_trip"
            }
        } else if let error = error as? OSDIError {
            switch error {
            case .native(let issue):
                native = issue
                // Preserve the pinned COSDI status while giving CLI callers an actionable category.
                switch issue.code {
                case 7, 20: reason = "resource_limit"
                case 12: reason = "unsupported_capability"
                case 13, 14, 15, 21: reason = "invalid_parameter"
                case 16: reason = "native_setup"
                case 17, 19: reason = "native_evaluation"
                case 18: reason = "invalid_unknowns"
                default: reason = "native_execution"
                }
            case .artifact: reason = "artifact_access"
            case .artifactClose: reason = "artifact_close"
            case .identityMismatch: reason = "identity_mismatch"
            case .cancelled: reason = "cancelled"
            case .resourceLimit: reason = "resource_limit"
            case .libraryInUse: reason = "library_in_use"
            case .closed: reason = "library_closed"
            case .contract: reason = "invalid_parameter_or_options"
            case .cleanup, .pendingCleanup, .cleanupRequired: reason = "cleanup_failure"
            default: break
            }
        } else if error is DecodingError { reason = "invalid_request" }
        return .init(reason: reason, detail: String(describing: error), nativeStatus: native?.code,
            descriptorIndex: native?.descriptorIndex, field: native?.field,
            modelFlags: native?.modelFlags, setupErrorCount: native?.setupErrorCount,
            setupErrorCode: native?.setupErrorCode, setupParameterID: native?.setupParameterID)
    }
}
