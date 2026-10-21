import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import Foundation
import OSDIContracts
import OSDIRuntime

/// Host serialization adapter for the existing precision owner; no solver or numeric fallback.
enum OSDIEvaluatePrecisionCommand {
    static func run(requestPath: String, outputPath: String) async -> Bool {
        var requestBinding: OSDIEvaluateReport.Binding?
        var rangeRequest = false
        do {
            let bytes = try read(URL(fileURLWithPath: requestPath), maximum: 16 * 1024 * 1024)
            requestBinding = try binding(Array(bytes))
            let request = try JSONDecoder().decode(OSDIEvaluatePrecisionRequest.self, from: bytes)
            rangeRequest = request.isRange
            try request.validate()
            let result = try await execute(request, binding: requestBinding!)
            try save(result, path: URL(fileURLWithPath: outputPath), maximum: request.maximumReportBytes)
            print(request.isRange ? "OSDI smooth-box enclosure passed; local F/Q/G/C bounds only" : request.operation == "snapshot" ? "OSDI precision snapshot passed; program/setup admission only" : "OSDI precision evaluation passed; enclosed F/delta-Q and inexact G/C only")
            return true
        } catch {
            let final = retryCleanup(error)
            let report = OSDIEvaluatePrecisionReport(binding: requestBinding, input: nil, failure: .init(final), rangeFailure: rangeRequest)
            do { try save(report, path: URL(fileURLWithPath: outputPath), maximum: 4 * 1024 * 1024) }
            catch { FileHandle.standardError.write(Data("Precision failure report write failed: \(error)\n".utf8)) }
            return false
        }
    }
    static func binding(_ bytes: [UInt8]) throws -> OSDIEvaluateReport.Binding {
        let count = UInt64(bytes.count)
        let limits = try ContentDigestSessionLimits(maximumChunkByteCount: max(1, count), maximumTotalByteCount: max(1, count), maximumUpdateCount: 1)
        let digest = try SHA256ContentDigester().digest(using: .sha256, limits: limits) {
            (lease: borrowing ContentDigestUpdateLease) throws(ContentDigestError) in try lease.update(bytes)
        }
        return .init(sha256: digest.digest.hexadecimalValue, byteCount: digest.totalByteCount)
    }
    private static func read(_ path: URL, maximum: UInt64) throws -> Data {
        guard maximum > 0, maximum < UInt64(Int.max) else { throw OSDIEvaluateFailure.invalidRequest("Invalid JSON read bound") }
        let file = try FileHandle(forReadingFrom: path)
        var result = Data(), failure: (any Error)?
        do {
            while true {
                let count = min(65536, Int(maximum) + 1 - result.count)
                guard let chunk = try file.read(upToCount: count), !chunk.isEmpty else { break }
                result.append(chunk)
                guard UInt64(result.count) <= maximum else { throw OSDIEvaluateFailure.invalidRequest("JSON byte limit exceeded") }
            }
        } catch { failure = error }
        do { try file.close() } catch { failure = combine(failure, error) }
        if let failure { throw failure }; return result
    }
    private static func intent(_ reference: ArtifactReference, file: URL, root: ArtifactRootID,
                               budget: OSDIEvaluateRequest.ReadBudget) throws -> ArtifactAccessIntent {
        try .init(expectedReference: reference, availability: .local(artifactID: reference.id, rootID: root,
            relativePath: ArtifactRelativePath(segments: [file.lastPathComponent])), operation: .read,
            budget: ArtifactAccessBudget(maximumPageByteCount: budget.maximumPageByteCount,
                maximumTotalByteCount: budget.maximumTotalByteCount, maximumPageCount: budget.maximumPageCount,
                maximumWorkUnitCount: budget.maximumWorkUnitCount, maximumDurationNanoseconds: budget.maximumDurationNanoseconds))
    }
    private static func execute(_ request: OSDIEvaluatePrecisionRequest, binding: OSDIEvaluateReport.Binding) async throws -> OSDIEvaluatePrecisionReport {
        let modelFile = URL(fileURLWithPath: request.model.libraryPath).standardizedFileURL
        let workerFile = URL(fileURLWithPath: request.worker.path).standardizedFileURL
        let rootID = try ArtifactRootID(rawValue: "osdi-evaluate-precision-model")
        let modelSource = try ArtifactRootCapability(rootID: rootID, directoryURL: modelFile.deletingLastPathComponent(), digester: SHA256ContentDigester())
        var failure: (any Error)?, result: OSDIEvaluatePrecisionReport?
        do {
            let workerID = try ArtifactRootID(rawValue: "osdi-evaluate-precision-worker")
            let workerSource = try ArtifactRootCapability(rootID: workerID, directoryURL: workerFile.deletingLastPathComponent(), digester: SHA256ContentDigester())
            do {
                let reference = try ArtifactReference(digest: .init(algorithm: .sha256, hexadecimalValue: request.model.expectedSHA256),
                    byteCount: request.model.expectedByteCount, descriptor: .init(role: .input, kind: .model, format: .init(rawValue: "osdi")))
                let limits = request.model.limits
                let staging = URL(fileURLWithPath: request.stagingParentDirectory, isDirectory: true)
                let library = try await OSDINativeLibraryLoader(stagingParentDirectory: staging).load(
                    intent(reference, file: modelFile, root: rootID, budget: request.model.readBudget), source: modelSource,
                    limits: OSDILoadLimits(maximumDescriptors: limits.maximumDescriptors, maximumMetadataBytes: limits.maximumMetadataBytes,
                        maximumStorageBytes: limits.maximumStorageBytes))
                do {
                    result = try await evaluate(request, binding: binding, library: library,
                        worker: intent(request.worker.reference, file: workerFile, root: workerID, budget: request.worker.readBudget), source: workerSource)
                } catch { failure = error }
                do { try library.close() } catch { failure = combine(failure, error) }
            } catch { failure = combine(failure, error) }
            do { try await workerSource.close().wait() } catch { failure = combine(failure, error) }
        } catch { failure = combine(failure, error) }
        do { try await modelSource.close().wait() } catch { failure = combine(failure, error) }
        if let failure { throw failure }
        guard let result else { throw OSDIEvaluateFailure.invalidRequest("Precision execution produced no report") }; return result
    }
    private static func evaluate(_ request: OSDIEvaluatePrecisionRequest, binding: OSDIEvaluateReport.Binding,
        library: borrowing OSDILibrary, worker: ArtifactAccessIntent, source: any ArtifactAccessing) async throws -> OSDIEvaluatePrecisionReport {
        let preparation = try request.model.preparationOptions(), options = try request.options.checked()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: request.model.descriptorIndex, options: preparation)
        defer { model.close() }
        let factory = OSDINativePrecisionSessionPreparer(stagingParentDirectory: URL(fileURLWithPath: request.stagingParentDirectory, isDirectory: true))
        let program = try factory.snapshot(library: library, descriptorIndex: request.model.descriptorIndex, model: model,
            preparation: preparation, semanticProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: request.semanticProfileSHA256), options: options)
        let prepared = OSDIEvaluateReport.Prepared(noiseSources: model.noiseSources.map(OSDIEvaluateReport.NoiseSource.init),
            topology: .init(model.topology), parameterMetadata: model.parameterMetadata.map(OSDIEvaluateReport.ParameterInfo.init),
            resolvedParameters: model.resolvedParameters.map(OSDIEvaluateReport.ResolvedParameter.init), diagnostics: model.diagnostics.map(OSDIEvaluateReport.Diagnostic.init))
        if request.operation == "evaluate", request.candidateHigh?.values.count != program.topology.activeUnknownCount {
            throw OSDIEvaluateFailure.invalidUnknowns("Precision coordinates must follow the complete prepared active map")
        }
        if request.isRange, request.lowerHigh?.values.count != program.topology.activeUnknownCount {
            throw OSDIEvaluateFailure.invalidUnknowns("Range bounds must follow the complete prepared active map")
        }
        // Admit the complete owned JSON boundary before copying result spans. UTF-8 JSON
        // escaping costs at most six bytes; numeric value+bit-pattern pairs reserve 64.
        var remaining = request.maximumReportBytes
        func charge(_ count: UInt64, factor: UInt64 = 1) throws {
            let (bytes, overflow) = count.multipliedReportingOverflow(by: factor)
            guard !overflow, bytes <= remaining else { throw OSDIError.resourceLimit }; remaining -= bytes
        }
        try charge(binding.byteCount, factor: 6)
        try charge(request.model.limits.maximumMetadataBytes, factor: 12)
        try charge(UInt64(program.programBytes.count), factor: 4); try charge(UInt64(program.setupBytes.count), factor: 4)
        let nodes = UInt64(program.topology.nodes.count)
        let (matrix, overflow) = nodes.multipliedReportingOverflow(by: nodes)
        guard !overflow else { throw OSDIError.resourceLimit }
        try charge(nodes, factor: (request.isRange ? 4 : 6) * 64)
        try charge(matrix, factor: (request.isRange ? 4 : 2) * 64); try charge(65536)
        let numericProfile = try ContentDigest(algorithm: .sha256, hexadecimalValue: request.numericProfileSHA256)
        var session: OSDIPrecisionSession
        if request.isRange {
            session = try await factory.prepareRange(program: program, worker: worker, source: source,
                numericProfile: numericProfile, options: options)
        } else {
            session = try await factory.prepare(program: program, worker: worker, source: source,
                numericProfile: numericProfile, options: options)
        }
        var failure: (any Error)?, evaluated: OSDIEvaluatePrecisionReport.Evaluation?
        var enclosed: OSDIEvaluatePrecisionReport.RangeEvaluation?
        do {
            if request.isRange {
                guard let lowerHigh = request.lowerHigh, let lowerLow = request.lowerLow,
                      let upperHigh = request.upperHigh, let upperLow = request.upperLow,
                      let lowerContext = request.lowerContext, let upperContext = request.upperContext else {
                    throw OSDIEvaluateFailure.invalidRequest("Range enclosure fields are absent")
                }
                enclosed = try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
                    lowerHigh: lowerHigh.values.span, lowerLow: lowerLow.values.span,
                    upperHigh: upperHigh.values.span, upperLow: upperLow.values.span,
                    lowerContext: lowerContext.checked(), upperContext: upperContext.checked(),
                    precisionBits: request.precisionBits) { view in OSDIEvaluatePrecisionReport.RangeEvaluation(view) }
            } else if request.operation == "evaluate" {
                guard let candidateHigh = request.candidateHigh, let candidateLow = request.candidateLow,
                      let referenceHigh = request.referenceHigh, let referenceLow = request.referenceLow,
                      let candidateContext = request.candidateContext, let referenceContext = request.referenceContext else {
                    throw OSDIEvaluateFailure.invalidRequest("Precision evaluation fields are absent")
                }
            evaluated = try OSDINativePrecisionEvaluator().evaluate(session: &session,
                candidateHigh: candidateHigh.values.span, candidateLow: candidateLow.values.span,
                referenceHigh: referenceHigh.values.span, referenceLow: referenceLow.values.span,
                candidateContext: candidateContext.checked(), referenceContext: referenceContext.checked(),
                precisionBits: request.precisionBits) { view in OSDIEvaluatePrecisionReport.Evaluation(view) }
            }
        } catch { failure = error }
        do { try session.close() } catch { failure = combine(failure, error) }
        if let failure { throw failure }
        guard request.operation == "snapshot" || (request.isRange ? enclosed != nil : evaluated != nil) else {
            throw OSDIEvaluateFailure.invalidUnknowns("Explicit numerical operation published no view")
        }
        let result = OSDIEvaluatePrecisionReport(binding: binding, input: request,
            program: .init(program), prepared: prepared, evaluation: evaluated, rangeEvaluation: enclosed)
        try result.validate(); return result
    }
    private static func combine(_ primary: (any Error)?, _ secondary: any Error) -> any Error {
        guard let primary else { return secondary }
        return OSDIError.cleanup(primary: (primary as? OSDIError) ?? .host(operation: "precision CLI", underlying: primary),
            secondary: (secondary as? OSDIError) ?? .host(operation: "precision CLI cleanup", underlying: secondary))
    }
    private static func retryCleanup(_ error: any Error) -> any Error {
        guard let typed = error as? OSDIError else { return error }
        switch typed {
        case .precisionCleanup(_, let owner):
            do { try owner.close(); return typed } catch { return combine(typed, error) }
        case .cleanupRequired(_, let owner), .pendingCleanup(_, _, let owner):
            do { try owner.close(); return typed } catch { return combine(typed, error) }
        case .cleanup(let primary, let secondary):
            return combine(retryCleanup(primary), retryCleanup(secondary))
        default: return typed
        }
    }
    private static func save(_ report: OSDIEvaluatePrecisionReport, path: URL, maximum: UInt64) throws {
        try report.validate()
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        let bytes = try encoder.encode(report)
        guard UInt64(bytes.count) <= maximum else { throw OSDIError.resourceLimit }
        try bytes.write(to: path, options: .atomic)
        let reread = try read(path, maximum: maximum)
        guard reread == bytes else { throw OSDIEvaluateFailure.reportRoundTrip }
        let restored = try JSONDecoder().decode(OSDIEvaluatePrecisionReport.self, from: reread)
        try restored.validate()
    }
}
