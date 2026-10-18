import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_PRECISION_QUALIFICATION"] == "1", "Requires the real worker and OSDI_DECISION_COVER_FIXTURES"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionDecisionCoverTests {
    private struct Manifest: Decodable {
        struct Entry: Decodable { let id, programPath, setupPath, programSHA256, setupSHA256: String }
        let fixtures: [Entry]
    }
    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else { throw EvaluationFixtureError.requiredEnvironment(key) }
        return value
    }
    private func profile() throws -> ContentDigest { try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256")) }
    private func policy(work: UInt64 = 100_000_000) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 4_194_304, arenaBytes: 4_194_304,
            maximumWorkUnits: work, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func limits(steps: UInt64 = 100_000, guards: UInt64 = 100_000, copies: UInt64 = 10_000_000, nodes: UInt32 = 256) throws -> OSDIPrecisionDecisionCoverLimits {
        try OSDIPrecisionDecisionCoverLimits(maximumSteps: steps, maximumGuardRecords: guards, maximumCopiedBytes: copies,
            maximumCells: 100, maximumNodes: nodes, maximumLeaves: 64, maximumPending: 256, maximumDepth: 256, maximumIntegerBits: 1025)
    }
    private func context(_ time: Double = 0) throws -> OSDIPrecisionContext { try OSDIPrecisionContext(mode: .transient, timeHigh: time, timeLow: 0) }
    private func fixture(_ name: String, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let url = URL(fileURLWithPath: try external("OSDI_DECISION_COVER_FIXTURES"), isDirectory: true).appendingPathComponent("manifest.json")
        let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: url))
        let entry = try #require(manifest.fixtures.first { $0.id == name })
        let bytes = Array(try Data(contentsOf: URL(fileURLWithPath: entry.programPath))), setup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
        let pd = try OSDIPrecisionProgramBuilder.digest(bytes), sd = try OSDIPrecisionProgramBuilder.digest(setup)
        try #require(pd.hexadecimalValue == entry.programSHA256 && sd.hexadecimalValue == entry.setupSHA256)
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let node = try OSDINodeInfo(index: 0, name: "x", units: "V", residualUnits: "A", isFlow: false,
            unknownNature: nature, residualNature: nature, activeUnknownIndex: 0)
        let topology = try OSDIPreparedTopology(nodes: [node], activeUnknownCount: 1, stateCount: 0)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: pd, setupDigest: sd, semanticProfile: pd)
    }
    private func evaluate<Result>(_ session: inout OSDIPrecisionSession, lower: Double = 1, upper: Double = 2,
        time: Double = 0, _ body: (OSDIPrecisionDecisionCoverEvaluationView) -> Result) throws -> Result {
        let lo = [lower], hi = [upper], zero = [0.0]
        let evaluator: any OSDIPrecisionDecisionCoverEvaluating = OSDINativePrecisionEvaluator()
        return try evaluator.constructDecisionCover(session: &session, lowerHigh: lo.span, lowerLow: zero.span,
            upperHigh: hi.span, upperLow: zero.span, lowerContext: context(time), upperContext: context(time), precisionBits: 192, body)
    }
    @Test func completeIrrationalEqualityAllLeavesBindingAndBorrowLifetime() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), preparer: any OSDIPrecisionDecisionCoverSessionPreparing = OSDINativePrecisionSessionPreparer()
        let program = try fixture("equality", reference: worker.intent.expectedReference)
        var session = try await preparer.prepareDecisionCover(program: program, worker: worker.intent, source: worker.source,
            numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        var address: UInt = 0
        let usage = try evaluate(&session, time: 0.25) { view in
            #expect(view.binding == OSDIPrecisionChartBinding(program: program) && view.nodes.count == 7 && view.leafCount == 3)
            #expect(view.nodes[0].parent == nil && view.nodes[0].event?.allowedMask == 14 && view.nodes[0].event?.decision.opcode == 58)
            #expect(view.lowerHigh[0] == 1 && view.upperHigh[0] == 2 && view.lowerContext.timeHigh == 0.25 && view.upperContext.timeHigh == 0.25)
            var equalityLeaves = 0
            for i in 0..<view.nodes.count {
                let node = view.nodes[i]
                if let leaf = node.leaf, let parent = node.parent {
                    let relation = view.nodes[Int(parent)].incoming?.choice
                    view.withLeaf(at: Int(leaf)) { values in
                        #expect(values.fLower.count == 1 && values.gLower.count == 1 && values.cUpper.count == 1)
                        if relation == 2 { #expect(values.fLower[0] == 7 && values.fUpper[0] == 7); equalityLeaves += 1 }
                        else { #expect(values.fLower[0] <= 1 && values.fUpper[0] >= 2) }
                        #expect(values.qLower[0] <= 1 && values.qUpper[0] >= 2 && values.gLower[0] == 1 && values.cLower[0] == 1)
                    }
                }
            }
            #expect(equalityLeaves == 1)
            view.withLeaf(at: 0) { address = $0.fLower.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) } }
            return (view.completedWorkUnits, view.completedSteps, view.completedGuardRecords, view.completedCopiedBytes)
        }
        try evaluate(&session, time: 0.25) { view in
            #expect(view.completedWorkUnits > usage.0 && view.completedSteps == 2*usage.1 && view.completedGuardRecords == 2*usage.2 && view.completedCopiedBytes == 2*usage.3)
            view.withLeaf(at: 0) { #expect($0.fLower.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) } == address) }
        }
        try session.close(); try session.close()
        var called = false
        do { try evaluate(&session) { _ in called = true }; Issue.record("Closed cover published") }
        catch OSDIPrecisionDecisionCoverError.runtime(.closed) {}
        #expect(!called)
    }
    @Test func exactArithmeticCellsAndUnsubmittedShapeOperationRefusals() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        var session = try await OSDINativePrecisionSessionPreparer().prepareDecisionCover(program: fixture("floor", reference: worker.intent.expectedReference),
            worker: worker.intent, source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        let zero = [0.0], empty: [Double] = []
        var called = false
        do { try OSDINativePrecisionEvaluator().constructDecisionCover(session: &session, lowerHigh: empty.span, lowerLow: zero.span,
            upperHigh: zero.span, upperLow: zero.span, lowerContext: context(), upperContext: context(), precisionBits: 192) { _ in called = true }; Issue.record("Wrong shape admitted") }
        catch OSDIPrecisionDecisionCoverError.runtime(.invalidRange) {}
        do { try OSDINativePrecisionEvaluator().evaluateRange(session: &session, lowerHigh: zero.span, lowerLow: zero.span,
            upperHigh: zero.span, upperLow: zero.span, lowerContext: context(), upperContext: context(), precisionBits: 192) { _ in called = true }; Issue.record("Wrong operation admitted") }
        catch OSDIError.invalidRange {}
        try evaluate(&session, lower: -1.25, upper: 1.25) { view in
            #expect(view.requestSequence == 1 && view.leafCount == 4 && view.nodes[0].event?.cellCount == 4)
            #expect(view.nodes[0].event?.firstCell.isNegative == true && view.nodes[0].event?.firstCell[0] == 2)
            #expect(view.nodes[0].event?.lastCell.isNegative == false && view.nodes[0].event?.lastCell[0] == 1)
            var seen: Set<Double> = []
            for i in 0..<view.leafCount { view.withLeaf(at: i) { #expect($0.fLower[0] == $0.fUpper[0]); seen.insert($0.fLower[0]) } }
            #expect(seen == [-2,-1,0,1])
        }
        #expect(!called); try session.close()
    }
    @Test func submittedResourceAndInvalidFormulaRefusalsPublishNoCover() async throws {
        let baseWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("equality", reference: baseWorker.intent.expectedReference)
        var baseline = try await OSDINativePrecisionSessionPreparer().prepareDecisionCover(program: program, worker: baseWorker.intent,
            source: baseWorker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await baseWorker.source.close().wait()
        let usage = try evaluate(&baseline) { ($0.completedSteps,$0.completedGuardRecords,$0.completedCopiedBytes,$0.completedWorkUnits) }
        try baseline.close()
        for failure in ["nodes","steps","guards","copies","work","correlated_invalid","sqrt"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let selected = try fixture(failure == "correlated_invalid" || failure == "sqrt" ? failure : "equality", reference: program.modelReference)
            var session = try await OSDINativePrecisionSessionPreparer().prepareDecisionCover(program: selected, worker: worker.intent, source: worker.source,
                numericProfile: profile(), options: policy(work: failure == "work" ? 2*usage.3-1 : 100_000_000),
                limits: limits(steps: failure == "steps" ? 2*usage.0-1 : 100_000, guards: failure == "guards" ? 2*usage.1-1 : 100_000,
                    copies: failure == "copies" ? 2*usage.2-1 : 10_000_000, nodes: failure == "nodes" ? 2 : 256))
            try await worker.source.close().wait()
            if ["steps","guards","copies","work"].contains(failure) { try evaluate(&session) { _ in } }
            var called = false
            do { try evaluate(&session, lower: failure == "correlated_invalid" ? -1 : failure == "sqrt" ? 0 : 1,
                upper: failure == "correlated_invalid" || failure == "sqrt" ? 1 : 2) { _ in called = true }; Issue.record("Refusal published: \(failure)") }
            catch OSDIPrecisionDecisionCoverError.capacityExceeded(let issue) { #expect(["nodes","guards","copies"].contains(failure) && (issue.workerStatus == 113 || issue.workerStatus == 116)) }
            catch OSDIPrecisionDecisionCoverError.stepLimitExceeded(let issue) { #expect(failure == "steps" && issue.workerStatus == 102) }
            catch OSDIPrecisionDecisionCoverError.runtime(.precision(let issue)) { #expect((failure == "work" && issue.workerStatus == 7) || (failure == "correlated_invalid" && issue.workerStatus == 4) || (failure == "sqrt" && issue.workerStatus == 10)) }
            #expect(!called); try session.close()
        }
    }
    @Test func cancellationAndOldWorkerNeverPublishOrFallback() async throws {
        let task = Task {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var session = try await OSDINativePrecisionSessionPreparer().prepareDecisionCover(program: fixture("equality", reference: worker.intent.expectedReference),
                worker: worker.intent, source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
            try await worker.source.close().wait(); withUnsafeCurrentTask { $0?.cancel() }
            var called = false
            do { try evaluate(&session) { _ in called = true }; Issue.record("Cancelled cover published") }
            catch OSDIPrecisionDecisionCoverError.runtime(.precision(let issue)) { #expect(issue.code == .cancelled) }
            try session.close(); return called
        }
        #expect(try await task.value == false)
        let old = try EvaluationArtifact(try await ProtocolFixtureCache.shared.fixtures().transportWorker.path)
        var published = false
        do {
            var session = try await OSDINativePrecisionSessionPreparer().prepareDecisionCover(program: fixture("equality", reference: old.intent.expectedReference),
                worker: old.intent, source: old.source, numericProfile: profile(), options: policy(), limits: limits())
            do { try evaluate(&session) { _ in published = true }; Issue.record("Old worker published cover") }
            catch OSDIPrecisionDecisionCoverError.runtime(.precision(let issue)) { #expect(issue.code == .workerFailure || issue.code == .protocolViolation) }
            try session.close()
        } catch OSDIPrecisionDecisionCoverError.runtime(.precision(let issue)) { #expect(issue.code == .workerFailure || issue.code == .protocolViolation) }
        #expect(!published); try await old.source.close().wait()
    }
    @Test func numericalFailureAndRetainedCleanupKeepBothCauses() async throws {
        let parent = FileManager.default.temporaryDirectory.appendingPathComponent("cover-cleanup-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: false)
        defer { do { try FileManager.default.removeItem(at: parent) } catch { Issue.record("Cleanup test release: \(error)") } }
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        var session = try await OSDINativePrecisionSessionPreparer(stagingParentDirectory: parent).prepareDecisionCover(
            program: fixture("correlated_invalid", reference: worker.intent.expectedReference), worker: worker.intent,
            source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        let entries = try FileManager.default.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil)
        let staged = try #require(entries.first); try #require(entries.count == 1); try FileManager.default.removeItem(at: staged)
        var called = false, retained = false
        do { try evaluate(&session, lower: -1, upper: 1) { _ in called = true }; Issue.record("Invalid source published") }
        catch OSDIPrecisionDecisionCoverError.cleanup(let primary, let secondary) {
            guard case .runtime(.precision(let issue)) = primary else { throw primary }; #expect(issue.workerStatus == 4)
            guard case .precisionCleanup(let cause, let owner) = secondary else { throw secondary }
            guard case .host(let operation, _) = cause else { throw cause }; #expect(operation == "remove precision staging directory")
            try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: false); try owner.close(); retained = true
        }
        #expect(!called && retained); try session.close()
    }
}
