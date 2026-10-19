import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_PRECISION_QUALIFICATION"] == "1", "Requires real worker and OSDI_CONSTITUTIVE_FIXTURES"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionConstitutiveTests {
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
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 16_777_216, arenaBytes: 16_777_216,
            maximumWorkUnits: work, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func coverLimits(copies: UInt64 = 10_000_000) throws -> OSDIPrecisionDecisionCoverLimits {
        try OSDIPrecisionDecisionCoverLimits(maximumSteps: 100_000, maximumGuardRecords: 100_000, maximumCopiedBytes: copies,
            maximumCells: 100, maximumNodes: 256, maximumLeaves: 64, maximumPending: 256, maximumDepth: 256, maximumIntegerBits: 1025)
    }
    private func identityLimits(bits: UInt64 = 4096, nodes: UInt32 = 256) throws -> OSDIPrecisionConstitutiveLimits {
        try OSDIPrecisionConstitutiveLimits(maximumNodes: nodes, maximumAtoms: 64,
            maximumCoefficientBits: bits, maximumWorkspaceBytes: 2_097_152)
    }
    private func context() throws -> OSDIPrecisionContext { try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0) }
    private func fixture(_ name: String, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf:
            URL(fileURLWithPath: external("OSDI_CONSTITUTIVE_FIXTURES")).appendingPathComponent("manifest.json")))
        let entry = try #require(manifest.fixtures.first { $0.id == name })
        let bytes = Array(try Data(contentsOf: URL(fileURLWithPath: entry.programPath)))
        let setup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
        let pd = try OSDIPrecisionProgramBuilder.digest(bytes), sd = try OSDIPrecisionProgramBuilder.digest(setup)
        try #require(pd.hexadecimalValue == entry.programSHA256 && sd.hexadecimalValue == entry.setupSHA256)
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let count = name == "common_kernel" ? 4 : 3
        let nodes = try (0..<count).map { try OSDINodeInfo(index: $0, name: "x\($0)", units: "V", residualUnits: "A", isFlow: false,
            unknownNature: nature, residualNature: nature, activeUnknownIndex: $0) }
        let topology = try OSDIPreparedTopology(nodes: nodes, activeUnknownCount: count, stateCount: 0)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: pd, setupDigest: sd, semanticProfile: pd)
    }
    private func evaluate<Result>(_ session: inout OSDIPrecisionSession, _ body: (OSDIPrecisionConstitutiveEvaluationView) -> Result) throws -> Result {
        let count = session.program.topology.activeUnknownCount
        let lo = [Double](repeating: -1, count: count), hi = [Double](repeating: 1, count: count), zero = [Double](repeating: 0, count: count)
        let evaluator: any OSDIPrecisionConstitutiveEvaluating = OSDINativePrecisionEvaluator()
        return try evaluator.constructConstitutiveIdentities(session: &session, lowerHigh: lo.span, lowerLow: zero.span,
            upperHigh: hi.span, upperLow: zero.span, lowerContext: context(), upperContext: context(), precisionBits: 192, body)
    }
    private func integer(_ claim: borrowing OSDIPrecisionConstitutiveClaimView, _ row: Int) -> Int64 {
        claim.withCoefficient(at: row) { value in
            #expect(value.denominatorPower == 0 && value.numeratorWords.count <= 1)
            let magnitude = value.numeratorWords.isEmpty ? 0 : Int64(value.numeratorWords[0])
            return value.negative ? -magnitude : magnitude
        }
    }
    @Test func exactFullRowIdentitiesAndBorrowedReuse() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("conservation", reference: worker.intent.expectedReference)
        let preparer: any OSDIPrecisionConstitutiveSessionPreparing = OSDINativePrecisionSessionPreparer()
        var session = try await preparer.prepareConstitutiveIdentities(program: program, worker: worker.intent, source: worker.source,
            numericProfile: profile(), options: policy(), coverLimits: coverLimits(), limits: identityLimits(),
            coordinateQuotient: .identity(topology: (program).topology))
        try await worker.source.close().wait()
        var address: UInt = 0
        let usage = try evaluate(&session) { view in
            #expect(view.cover.binding == OSDIPrecisionChartBinding(program: program) && view.cover.leafCount == 1 && view.claimCount == 4)
            #expect(view.cover.lowerHigh[0] == -1 && view.cover.upperHigh[0] == 1 && view.cover.lowerContext.timeHigh == 0.25)
            var f = 0, q = 0
            for i in 0..<view.claimCount { view.withClaim(at: i) { claim in
                #expect(claim.rawRowCount == 3)
                let a = integer(claim, 0), b = integer(claim, 1), c = integer(claim, 2)
                if claim.kind == .resistive { #expect(2*a + 3*b - 5*c == 0); f += 1 }
                else { #expect(a == b); q += 1 }
                if i == 0 { claim.withCoefficient(at: 0) { value in
                    address = value.numeratorWords.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) }
                }}
            }}
            #expect(f == 2 && q == 2)
            return (view.cover.completedWorkUnits, view.cover.completedCopiedBytes)
        }
        try evaluate(&session) { view in
            #expect(view.cover.requestSequence == 2 && view.cover.completedWorkUnits > usage.0 && view.cover.completedCopiedBytes > usage.1)
            view.withClaim(at: 0) { $0.withCoefficient(at: 0) { value in
                #expect(value.numeratorWords.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) } == address)
            }}
        }
        try session.close(); try session.close()
        var called = false
        do { try evaluate(&session) { _ in called = true }; Issue.record("Closed identity session published") }
        catch OSDIPrecisionConstitutiveError.runtime(.closed) {}
        #expect(!called)
    }
    @Test func commonCombinationSurvivesEveryTerminal() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("common_kernel", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(
            program: program, worker: worker.intent, source: worker.source,
            numericProfile: profile(), options: policy(), coverLimits: coverLimits(), limits: identityLimits(),
            coordinateQuotient: .identity(topology: program.topology))
        try await worker.source.close().wait()
        try evaluate(&session) { view in
            #expect(view.cover.leafCount > 1 && view.claimCount == 5)
            var q = 0
            for i in 0..<view.claimCount { view.withClaim(at: i) { claim in
                if claim.kind == .reactive {
                    q += 1; let a = integer(claim, 0)
                    #expect(a != 0 && integer(claim, 1) == -a && integer(claim, 2) == a && integer(claim, 3) == -a)
                }
            }}
            #expect(q == 1)
        }
        try session.close()
    }
    @Test func growthCapacityWrongOperationAndCancellationNeverPublish() async throws {
        for variant in ["growth", "nodes"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let program = try fixture(variant == "growth" ? "growth" : "conservation", reference: worker.intent.expectedReference)
            var session = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(
                program: program,
                worker: worker.intent, source: worker.source, numericProfile: profile(), options: policy(), coverLimits: coverLimits(),
                limits: identityLimits(bits: variant == "growth" ? 128 : 4096, nodes: variant == "nodes" ? 1 : 256),
                coordinateQuotient: .identity(topology: program.topology))
            try await worker.source.close().wait(); var called = false
            do { try evaluate(&session) { _ in called = true }; Issue.record("Resource refusal published") }
            catch OSDIPrecisionConstitutiveError.coefficientLimitExceeded(let issue) { #expect(variant == "growth" && issue.workerStatus == 122) }
            catch OSDIPrecisionConstitutiveError.capacityExceeded(let issue) { #expect(variant == "nodes" && issue.workerStatus == 121) }
            #expect(!called); try session.close()
        }
        let task = Task {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let program = try fixture("conservation", reference: worker.intent.expectedReference)
            var session = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(
                program: program, worker: worker.intent, source: worker.source,
                numericProfile: profile(), options: policy(), coverLimits: coverLimits(), limits: identityLimits(),
                coordinateQuotient: .identity(topology: program.topology))
            try await worker.source.close().wait()
            let zero = [Double](repeating: 0, count: 3); var called = false
            do { try OSDINativePrecisionEvaluator().constructDecisionCover(session: &session, lowerHigh: zero.span, lowerLow: zero.span,
                upperHigh: zero.span, upperLow: zero.span, lowerContext: context(), upperContext: context(), precisionBits: 192) { _ in called = true }
                Issue.record("Wrong operation admitted") }
            catch OSDIPrecisionDecisionCoverError.runtime(.invalidRange) {}
            withUnsafeCurrentTask { $0?.cancel() }
            do { try evaluate(&session) { _ in called = true }; Issue.record("Cancelled identity published") }
            catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.code == .cancelled) }
            try session.close(); return called
        }
        #expect(try await task.value == false)
    }
    @Test func cumulativeWorkAndCopyQuotasRefuseSecondAttempt() async throws {
        let firstWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("conservation", reference: firstWorker.intent.expectedReference)
        var baseline = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(program: program,
            worker: firstWorker.intent, source: firstWorker.source, numericProfile: profile(), options: policy(),
            coverLimits: coverLimits(), limits: identityLimits(),
            coordinateQuotient: .identity(topology: (program).topology))
        try await firstWorker.source.close().wait()
        let first = try evaluate(&baseline) { ($0.cover.completedWorkUnits, $0.cover.completedCopiedBytes) }
        let second = try evaluate(&baseline) { ($0.cover.completedWorkUnits, $0.cover.completedCopiedBytes) }
        #expect(second.0 > first.0 && second.1 > first.1); try baseline.close()
        for failure in ["work", "copies"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var session = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(program: program,
                worker: worker.intent, source: worker.source, numericProfile: profile(),
                options: policy(work: failure == "work" ? second.0-1 : 100_000_000),
                coverLimits: coverLimits(copies: failure == "copies" ? second.1-1 : 10_000_000), limits: identityLimits(),
            coordinateQuotient: .identity(topology: (program).topology))
            try await worker.source.close().wait(); try evaluate(&session) { _ in }
            var called = false
            do { try evaluate(&session) { _ in called = true }; Issue.record("Cumulative quota reset") }
            catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(failure == "work" && issue.workerStatus == 7) }
            catch OSDIPrecisionConstitutiveError.cover(.capacityExceeded(let issue)) { #expect(failure == "copies" && (issue.workerStatus == 113 || issue.workerStatus == 116)) }
            catch OSDIPrecisionConstitutiveError.capacityExceeded(let issue) { #expect(failure == "copies" && issue.workerStatus == 121) }
            #expect(!called); try session.close()
        }
    }
    @Test func oldWorkerRefusesNewOperationAndCleanupRetainsPrimaryCause() async throws {
        let old = try EvaluationArtifact(try await ProtocolFixtureCache.shared.fixtures().transportWorker.path)
        let oldProgram = try fixture("conservation", reference: old.intent.expectedReference)
        do {
            var session = try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(
                program: oldProgram, worker: old.intent, source: old.source,
                numericProfile: profile(), options: policy(), coverLimits: coverLimits(), limits: identityLimits(),
                coordinateQuotient: .identity(topology: oldProgram.topology))
            do { try evaluate(&session) { _ in Issue.record("Old worker published identities") }; Issue.record("Old protocol admitted") }
            catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.code == .workerFailure || issue.code == .protocolViolation) }
            try session.close()
        } catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.code == .workerFailure || issue.code == .protocolViolation) }
        try await old.source.close().wait()
        let parent = FileManager.default.temporaryDirectory.appendingPathComponent("identity-cleanup-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: false)
        defer { do { try FileManager.default.removeItem(at: parent) } catch { Issue.record("Cleanup test release: \(error)") } }
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("growth", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer(stagingParentDirectory: parent).prepareConstitutiveIdentities(
            program: program, worker: worker.intent, source: worker.source,
            numericProfile: profile(), options: policy(), coverLimits: coverLimits(), limits: identityLimits(bits: 128),
            coordinateQuotient: .identity(topology: program.topology))
        try await worker.source.close().wait()
        let entries = try FileManager.default.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil)
        let staged = try #require(entries.first); try #require(entries.count == 1); try FileManager.default.removeItem(at: staged)
        var called = false, retained = false
        do { try evaluate(&session) { _ in called = true }; Issue.record("Growth failure published") }
        catch OSDIPrecisionConstitutiveError.cleanup(let primary, let secondary) {
            guard case .coefficientLimitExceeded(let issue) = primary else { throw primary }; #expect(issue.workerStatus == 122)
            guard case .precisionCleanup(_, let owner) = secondary else { throw secondary }
            try FileManager.default.createDirectory(at: staged, withIntermediateDirectories: false); try owner.close(); retained = true
        }
        #expect(!called && retained); try session.close()
    }

}
