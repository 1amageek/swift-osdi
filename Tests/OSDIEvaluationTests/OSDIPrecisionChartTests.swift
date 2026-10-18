import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_PRECISION_QUALIFICATION"] == "1", "Requires the real GMP/MPFR worker and OSDI_CHART_FIXTURES"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionChartTests {
    private struct Manifest: Decodable {
        struct Entry: Decodable {
            let id: String
            let programPath: String
            let setupPath: String
            let programSHA256: String
            let setupSHA256: String
        }
        let fixtures: [Entry]
    }
    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else {
            throw EvaluationFixtureError.requiredEnvironment(key)
        }
        return value
    }
    private func policy(work: UInt64 = 10_000_000) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 1_048_576,
            arenaBytes: 1_048_576, maximumWorkUnits: work, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 1_000_000_000, evaluationTimeoutNanoseconds: 1_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func limits(steps: UInt64 = 10_000, guards: UInt64 = 100) throws -> OSDIPrecisionChartLimits {
        try OSDIPrecisionChartLimits(maximumSteps: steps, maximumTapeEntries: 100, maximumGuardRecords: guards)
    }
    private func profile() throws -> ContentDigest {
        try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256"))
    }
    private func fixture(_ name: String, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let path = URL(fileURLWithPath: try external("OSDI_CHART_FIXTURES"), isDirectory: true).appendingPathComponent("manifest.json")
        let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: path))
        let entry = try #require(manifest.fixtures.first { $0.id == name })
        let bytes = Array(try Data(contentsOf: URL(fileURLWithPath: entry.programPath)))
        let setup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
        let programDigest = try OSDIPrecisionProgramBuilder.digest(bytes), setupDigest = try OSDIPrecisionProgramBuilder.digest(setup)
        try #require(programDigest.hexadecimalValue == entry.programSHA256 && setupDigest.hexadecimalValue == entry.setupSHA256)
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let node = try OSDINodeInfo(index: 0, name: "x", units: "V", residualUnits: "A", isFlow: false,
            unknownNature: nature, residualNature: nature, activeUnknownIndex: 0)
        let topology = try OSDIPreparedTopology(nodes: [node], activeUnknownCount: 1, stateCount: 0)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: programDigest, setupDigest: setupDigest,
            semanticProfile: programDigest)
    }
    private func context() throws -> OSDIPrecisionContext {
        try OSDIPrecisionContext(mode: .transient, timeHigh: 0, timeLow: 0)
    }
    private func evaluate<Result>(_ session: inout OSDIPrecisionSession, x: Double,
        mode: OSDIPrecisionChartMode = .record, tape: OSDIPrecisionChartTape? = nil,
        _ body: (OSDIPrecisionChartEvaluationView) -> Result) throws -> Result {
        let high = [x], low = [0.0]
        let evaluator: any OSDIPrecisionChartEvaluating = OSDINativePrecisionEvaluator()
        return try evaluator.evaluateChart(session: &session, lowerHigh: high.span, lowerLow: low.span,
            upperHigh: high.span, upperLow: low.span, lowerContext: context(), upperContext: context(),
            precisionBits: 192, mode: mode, tape: tape, body)
    }
    private func retainDecisions(_ view: borrowing OSDIPrecisionChartEvaluationView) -> [OSDIPrecisionChartDecision] {
        var decisions: [OSDIPrecisionChartDecision] = []
        for index in 0..<view.guards.count { decisions.append(view.guards[index].decision) }
        return decisions
    }

    @Test func exactEqualityAndFiniteFormulaClosureStayDistinctAcrossLeaseClosureAndReplay() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("jump", reference: worker.intent.expectedReference)
        let preparer: any OSDIPrecisionChartSessionPreparing = OSDINativePrecisionSessionPreparer()
        var session = try await preparer.prepareChart(program: program, worker: worker.intent,
            source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        var work: UInt64 = 0, steps: UInt64 = 0, address: UInt = 0
        let decisions = try evaluate(&session, x: 0.75) { view in
            #expect(view.binding == sessionBinding(program) && view.semantics == .record)
            #expect(view.guards.count == 3 && view.guards[0].decision.choice && view.guards[0].opcode == 54)
            #expect(view.requestSequence == 1 && view.completedSteps > 0 && view.completedWorkUnits > 0)
            work = view.completedWorkUnits; steps = view.completedSteps
            address = view.fLower.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) }
            return retainDecisions(view)
        }
        let tape = try OSDIPrecisionChartTape(binding: session.chartBinding, decisions: decisions)
        try evaluate(&session, x: 0.75, mode: .exactGraph, tape: tape) { view in
            #expect(view.semantics == .exactGraph && view.completedWorkUnits > work && view.completedSteps == 2*steps)
            #expect(view.fLower.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) } == address)
        }
        try evaluate(&session, x: 0.5, mode: .formulaClosure, tape: tape) { view in
            #expect(view.semantics == .formulaClosure && view.guards[0].boundaryExtension)
            #expect(view.qLower[0] == 0.75 && view.qUpper[0] == 0.75 && view.fLower[0] == 2)
        }
        try session.close(); try session.close()
        var called = false
        do { try evaluate(&session, x: 0.5) { _ in called = true }; Issue.record("Closed chart published a view") }
        catch OSDIPrecisionChartError.runtime(.closed) {}
        #expect(!called)

        let equalityWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        var equalitySession = try await preparer.prepareChart(program: program, worker: equalityWorker.intent,
            source: equalityWorker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await equalityWorker.source.close().wait()
        try evaluate(&equalitySession, x: 0.5) { view in
            #expect(!view.guards[0].decision.choice && !view.guards[0].boundaryExtension)
            #expect(view.qLower[0] == 0.625 && view.fLower[0] == 1)
        }
        do {
            try evaluate(&equalitySession, x: 0.5, mode: .exactGraph, tape: tape) { _ in called = true }
            Issue.record("Actual equality branch accepted incompatible right tape")
        } catch OSDIPrecisionChartError.tapeMismatch(let issue) { #expect(issue.workerStatus == 100) }
        #expect(!called); try equalitySession.close()
    }
    private func sessionBinding(_ program: OSDIPrecisionProgram) -> OSDIPrecisionChartBinding {
        OSDIPrecisionChartBinding(program: program)
    }

    @Test func bindingShapeAndOperationRefusalsPreserveUnsubmittedSession() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("jump", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer().prepareChart(program: program, worker: worker.intent,
            source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        let different = OSDIPrecisionProgram(modelReference: program.modelReference, descriptorIndex: program.descriptorIndex,
            topology: program.topology, programBytes: program.programBytes, setupBytes: program.setupBytes,
            programDigest: program.programDigest, setupDigest: program.programDigest, semanticProfile: program.semanticProfile)
        let wrong = try OSDIPrecisionChartTape(binding: sessionBinding(different), decisions: [])
        var called = false
        do {
            try evaluate(&session, x: 0.75, mode: .exactGraph, tape: wrong) { _ in called = true }
            Issue.record("Different setup binding accepted")
        } catch OSDIPrecisionChartError.runtime(.identityMismatch) {}
        let zero = [0.0], empty: [Double] = []
        do {
            try OSDINativePrecisionEvaluator().evaluateChart(session: &session,
                lowerHigh: empty.span, lowerLow: zero.span, upperHigh: zero.span, upperLow: zero.span,
                lowerContext: context(), upperContext: context(), precisionBits: 192, mode: .record, tape: nil) { _ in called = true }
            Issue.record("Wrong shape accepted")
        } catch OSDIPrecisionChartError.runtime(.invalidRange) {}
        do {
            try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
                lowerHigh: zero.span, lowerLow: zero.span, upperHigh: zero.span, upperLow: zero.span,
                lowerContext: context(), upperContext: context(), precisionBits: 192) { _ in called = true }
            Issue.record("Range accepted chart session")
        } catch OSDIError.invalidRange {}
        #expect(!called)
        try evaluate(&session, x: 0.75) { #expect($0.requestSequence == 1) }
        try session.close()
    }

    @Test func submittedTapeCapacityCumulativeStepsWorkAndUndefinedFormulaFailuresPublishNothing() async throws {
        let baselineWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("jump", reference: baselineWorker.intent.expectedReference)
        var baseline = try await OSDINativePrecisionSessionPreparer().prepareChart(program: program, worker: baselineWorker.intent,
            source: baselineWorker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await baselineWorker.source.close().wait()
        let usage = try evaluate(&baseline, x: 0.75) { ($0.completedSteps, $0.completedWorkUnits, retainDecisions($0)) }
        try baseline.close()
        for failure in ["tape", "guards", "steps", "work", "sqrt", "removable"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let selected = try fixture(failure == "sqrt" || failure == "removable" ? failure : "jump", reference: program.modelReference)
            var session = try await OSDINativePrecisionSessionPreparer().prepareChart(program: selected, worker: worker.intent,
                source: worker.source, numericProfile: profile(), options: policy(work: failure == "work" ? 2*usage.1-1 : 10_000_000),
                limits: limits(steps: failure == "steps" ? 2*usage.0-1 : 10_000, guards: failure == "guards" ? 1 : 100))
            try await worker.source.close().wait()
            if failure == "steps" || failure == "work" { try evaluate(&session, x: 0.75) { _ in } }
            var decisions = usage.2
            if failure == "tape" {
                let first = decisions[0]
                decisions[0] = OSDIPrecisionChartDecision(stage: first.stage, block: first.block+1,
                    instruction: first.instruction, kind: first.kind, choice: first.choice, ordinal: first.ordinal)
            }
            let tape = try OSDIPrecisionChartTape(binding: session.chartBinding, decisions: decisions)
            var called = false
            do {
                try evaluate(&session, x: failure == "sqrt" || failure == "removable" ? 0 : 0.75,
                    mode: failure == "tape" ? .exactGraph : .record, tape: failure == "tape" ? tape : nil) { _ in called = true }
                Issue.record("Submitted refusal published success: \(failure)")
            } catch OSDIPrecisionChartError.tapeMismatch(let issue) { #expect(failure == "tape" && issue.workerStatus == 100) }
            catch OSDIPrecisionChartError.guardCapacityExceeded(let issue) { #expect(failure == "guards" && issue.workerStatus == 101) }
            catch OSDIPrecisionChartError.stepLimitExceeded(let issue) { #expect(failure == "steps" && issue.workerStatus == 102) }
            catch OSDIPrecisionChartError.runtime(.precision(let issue)) {
                #expect((failure == "work" && issue.workerStatus == 7) || failure == "sqrt" || failure == "removable")
            }
            #expect(!called); try session.close()
        }
    }

    @Test func cancellationClosesWithoutPublishingAndOldWorkerDoesNotFallback() async throws {
        let task = Task {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var session = try await OSDINativePrecisionSessionPreparer().prepareChart(
                program: fixture("jump", reference: worker.intent.expectedReference), worker: worker.intent,
                source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
            try await worker.source.close().wait()
            withUnsafeCurrentTask { $0?.cancel() }
            var called = false
            do { try evaluate(&session, x: 0.75) { _ in called = true }; Issue.record("Cancelled chart published") }
            catch OSDIPrecisionChartError.runtime(.precision(let issue)) { #expect(issue.code == .cancelled) }
            try session.close(); return called
        }
        #expect(try await task.value == false)
        let old = try EvaluationArtifact(try await ProtocolFixtureCache.shared.fixtures().transportWorker.path)
        var oldPublished = false
        do {
            var session = try await OSDINativePrecisionSessionPreparer().prepareChart(
                program: fixture("jump", reference: old.intent.expectedReference), worker: old.intent,
                source: old.source, numericProfile: profile(), options: policy(), limits: limits())
            // Historical point-only transport oracles can emit a ready byte
            // without inspecting the version. That is not chart execution.
            // A real chart request must still refuse before any callback.
            do {
                try evaluate(&session, x: 0.75) { _ in oldPublished = true }
                Issue.record("Point-only worker published a chart result")
            } catch OSDIPrecisionChartError.runtime(.precision(let issue)) {
                #expect(issue.code == .workerFailure || issue.code == .protocolViolation)
            }
            try session.close()
        } catch OSDIPrecisionChartError.runtime(.precision(let issue)) {
            #expect(issue.code == .workerFailure || issue.code == .protocolViolation)
        }
        #expect(!oldPublished)
        try await old.source.close().wait()
    }

    @Test func chartPrimaryAndRetryableCleanupFailureRetainBothCauses() async throws {
        let parent = FileManager.default.temporaryDirectory.appendingPathComponent("chart-cleanup-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: false)
        defer { do { try FileManager.default.removeItem(at: parent) } catch { Issue.record("Cleanup test directory release: \(error)") } }
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("jump", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer(stagingParentDirectory: parent).prepareChart(
            program: program, worker: worker.intent, source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        let decisions = try evaluate(&session, x: 0.75) { retainDecisions($0) }
        let tape = try OSDIPrecisionChartTape(binding: session.chartBinding, decisions: decisions)
        let directories = try FileManager.default.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil)
        let staged = try #require(directories.first)
        try #require(directories.count == 1)
        // Deliberately remove a resource still owned by cleanup. Recreate its exact
        // directory only after the failure, so retained cleanup can be retried.
        try FileManager.default.removeItem(at: staged)
        var called = false, retained = false
        do {
            try evaluate(&session, x: 0.5, mode: .exactGraph, tape: tape) { _ in called = true }
            Issue.record("Incompatible equality chart published")
        } catch OSDIPrecisionChartError.cleanup(let primary, let secondary) {
            guard case .tapeMismatch(let issue) = primary else { throw primary }
            #expect(issue.workerStatus == 100)
            guard case .precisionCleanup(let cause, let owner) = secondary else { throw secondary }
            guard case .host(let operation, _) = cause else { throw cause }
            #expect(operation == "remove precision staging directory")
            try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: false)
            try owner.close(); retained = true
        }
        #expect(!called && retained); try session.close()
    }

    @Test func exactPrimitiveInfinityFactsSurviveCopiesAndFiniteProjectionOverflowStillRefuses() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("sentinels", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer().prepareChart(program: program, worker: worker.intent,
            source: worker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await worker.source.close().wait()
        let decisions = try evaluate(&session, x: 0.25) { view in
            #expect(view.guards.count == 4 && view.fLower[0] == 0.25 && view.qLower[0] == 0.25)
            let lower = view.guards[0].a, upper = view.guards[1].b
            #expect(lower.kind == .negativePrimitiveInfinity && lower.lower == -.infinity && lower.upper == -.infinity)
            #expect(upper.kind == .positivePrimitiveInfinity && upper.lower == .infinity && upper.upper == .infinity)
            #expect(lower.sourcePrimitiveConstantIndex == 8 && upper.sourcePrimitiveConstantIndex == 7)
            #expect(upper.register != nil && view.guards[1].decision.choice)
            return retainDecisions(view)
        }
        let tape = try OSDIPrecisionChartTape(binding: session.chartBinding, decisions: decisions)
        try evaluate(&session, x: 0.75, mode: .exactGraph, tape: tape) { view in
            #expect(view.fLower[0] == 0.75 && view.qLower[0] == 0.75)
        }
        try session.close()
        let overflowWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        var overflowing = try await OSDINativePrecisionSessionPreparer().prepareChart(
            program: fixture("overflow", reference: overflowWorker.intent.expectedReference), worker: overflowWorker.intent,
            source: overflowWorker.source, numericProfile: profile(), options: policy(), limits: limits())
        try await overflowWorker.source.close().wait()
        var called = false
        do { try evaluate(&overflowing, x: 0.25) { _ in called = true }; Issue.record("Finite MPFR guard overflow became primitive infinity") }
        catch OSDIPrecisionChartError.runtime(.precision(let issue)) { #expect(issue.workerStatus == 8) }
        #expect(!called); try overflowing.close()
    }
}
