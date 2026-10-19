import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_PRECISION_QUALIFICATION"] == "1", "Requires the real worker and qualified source fixtures"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionConstitutiveQuotientTests {
    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else { throw EvaluationFixtureError.requiredEnvironment(key) }
        return value
    }
    private func topology(flow: Bool = false, thermal: Bool = false, residualMismatch: Bool = false) throws -> OSDIPreparedTopology {
        let electrical = try OSDINatureReference(kind: 0, index: 0), temperature = try OSDINatureReference(kind: 0, index: 1)
        let nodes = try (0..<3).map { index in
            try OSDINodeInfo(index: index, name: ["D", "S", "B"][index], units: thermal && index == 1 ? "K" : "V",
                residualUnits: residualMismatch && index == 1 ? "W" : "A", isFlow: flow && index == 1,
                unknownNature: thermal && index == 1 ? temperature : electrical,
                residualNature: residualMismatch && index == 1 ? temperature : electrical, activeUnknownIndex: index)
        }
        return try OSDIPreparedTopology(nodes: nodes, activeUnknownCount: 3, stateCount: 0)
    }
    private func fixture(_ name: String = "probe-distinct", reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let directory = URL(fileURLWithPath: try external("OSDI_CONSTITUTIVE_FIXTURES"))
        // The C qualification driver emits this same raw SSA for independent
        // original-setup and explicit-coordinate-condition checker falsifiers.
        let bytes = Array(try Data(contentsOf: directory.appendingPathComponent(name + ".program")))
        let setup = Array(try Data(contentsOf: directory.appendingPathComponent(name + ".setup")))
        let pd = try OSDIPrecisionProgramBuilder.digest(bytes), sd = try OSDIPrecisionProgramBuilder.digest(setup)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: try topology(),
            programBytes: bytes, setupBytes: setup, programDigest: pd, setupDigest: sd, semanticProfile: pd)
    }
    private func policy(work: UInt64 = 100_000_000) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 16_777_216, arenaBytes: 16_777_216,
            maximumWorkUnits: work, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func prepare(_ program: OSDIPrecisionProgram, _ worker: EvaluationArtifact,
        _ quotient: OSDIPrecisionCoordinateQuotient, work: UInt64 = 100_000_000) async throws -> OSDIPrecisionSession {
        try await OSDINativePrecisionSessionPreparer().prepareConstitutiveIdentities(program: program, worker: worker.intent,
            source: worker.source, numericProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256")),
            options: policy(work: work), coverLimits: OSDIPrecisionDecisionCoverLimits(maximumSteps: 100_000,
                maximumGuardRecords: 100_000, maximumCopiedBytes: 10_000_000, maximumCells: 100, maximumNodes: 256,
                maximumLeaves: 64, maximumPending: 256, maximumDepth: 256, maximumIntegerBits: 1025),
            limits: OSDIPrecisionConstitutiveLimits(maximumNodes: 256, maximumAtoms: 64,
                maximumCoefficientBits: 4096, maximumWorkspaceBytes: 2_097_152), coordinateQuotient: quotient)
    }
    private func evaluate<Result>(_ session: inout OSDIPrecisionSession, lower: [Double] = [-1,-1,-1],
        upper: [Double] = [1,1,1], lowerLow: [Double] = [0,0,0], upperLow: [Double] = [0,0,0],
        _ body: (OSDIPrecisionConstitutiveEvaluationView) -> Result) throws -> Result {
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0)
        return try OSDINativePrecisionEvaluator().constructConstitutiveIdentities(session: &session,
            lowerHigh: lower.span, lowerLow: lowerLow.span, upperHigh: upper.span, upperLow: upperLow.span,
            lowerContext: context, upperContext: context, precisionBits: 192, body)
    }
    @Test func projectedDrainSourceAndGroundRetainExactConditionalBinding() async throws {
        var bindings: [OSDIPrecisionConstitutiveBinding] = []
        for map in [[UInt32(0),1,2], [UInt32(0),0,2], [UInt32.max,1,2]] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), program = try fixture(reference: worker.intent.expectedReference)
            let quotient = try OSDIPrecisionCoordinateQuotient(topology: program.topology, representatives: map)
            var session = try await prepare(program, worker, quotient); try await worker.source.close().wait()
            let binding = try evaluate(&session) { view in
                let binding = view.binding
                #expect(binding.source == OSDIPrecisionChartBinding(program: program) && binding.coordinateQuotient == quotient)
                #expect(view.cover.lowerHigh[0] == -1 && view.cover.upperHigh[1] == 1 && view.cover.leafCount == 1)
                var f = 0, q = 0
                for i in 0..<view.claimCount { view.withClaim(at: i) { claim in
                    #expect(claim.binding == binding && claim.rawRowCount == 3)
                    if claim.kind == .resistive { f += 1 } else { q += 1 }
                }}
                #expect(f == (map[0] == UInt32.max ? 3 : 2))
                #expect(q == (map[1] == 0 ? 3 : 2))
                return binding
            }
            bindings.append(binding); try session.close(); try session.close()
        }
        #expect(bindings[0].source == bindings[1].source && bindings[1].source == bindings[2].source)
        #expect(bindings[0] != bindings[1] && bindings[1] != bindings[2])
    }
    @Test func malformedFlowNatureAndDifferentTopologyAreRefusedAtAdmission() async throws {
        let base = try topology()
        for map in [[UInt32(0),0,1], [UInt32(1),1,2]] {
            #expect(throws: OSDIContractError.invalidTopology) { try OSDIPrecisionCoordinateQuotient(topology: base, representatives: map) }
        }
        for different in [try topology(flow: true), try topology(thermal: true), try topology(residualMismatch: true)] {
            #expect(throws: OSDIContractError.invalidTopology) { try OSDIPrecisionCoordinateQuotient(topology: different, representatives: [0,0,2]) }
        }
        let flow = try topology(flow: true)
        #expect(throws: OSDIContractError.invalidTopology) { try OSDIPrecisionCoordinateQuotient(topology: flow, representatives: [0,UInt32.max,2]) }
        let identity = try OSDIPrecisionCoordinateQuotient.identity(topology: flow)
        #expect(identity.representatives == [0,1,2])
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), program = try fixture(reference: worker.intent.expectedReference)
        let wrong = try OSDIPrecisionCoordinateQuotient.identity(topology: topology(thermal: true))
        do { var session = try await prepare(program, worker, wrong); try session.close(); Issue.record("Different-topology condition admitted") }
        catch OSDIPrecisionConstitutiveError.runtime(.contract(.invalidTopology)) {}
        try await worker.source.close().wait()
    }
    @Test func correlatedGroundDoesNotOmitAnyOriginalSourceTerminal() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), program = try fixture("branch", reference: worker.intent.expectedReference)
        let condition = try OSDIPrecisionCoordinateQuotient(topology: program.topology, representatives: [UInt32.max,1,2])
        var session = try await prepare(program, worker, condition); try await worker.source.close().wait()
        try evaluate(&session) { view in
            let originalLeaves = view.cover.leafCount
            var q = 0
            for i in 0..<view.claimCount { view.withClaim(at: i) { claim in
                if claim.kind == .reactive {
                    q += 1
                    claim.withCoefficient(at: 1) { value in
                        let count = value.numeratorWords.count, negative = value.negative, power = value.denominatorPower
                        #expect(count == 0 && !negative && power == 0,
                            "Q row 1 must remain constrained by the positive source leaf: words=\(count), negative=\(negative), power=\(power)")
                    }
                }
            }}
            // E substitutes x=0 into z=x*x*x+x, making Q0 exactly zero.
            // The original positive leaf still contributes Q1=-z+1=1; the
            // other leaves have Q1=-z=0. The common kernel is exactly c1=0,
            // with the two independent zero rows Q0 and Q2 retained.
            #expect(originalLeaves > 1 && q == 2, "Original leaves=\(originalLeaves), common Q kernel dimension=\(q)")
            #expect(view.cover.lowerHigh[0] == -1 && view.cover.upperHigh[0] == 1)
        }
        try session.close()
    }
    @Test func emptyPairedIntersectionNeverPublishesAndClosesSession() async throws {
        for variant in 0..<3 {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), program = try fixture(reference: worker.intent.expectedReference)
            let map: [UInt32] = variant == 2 ? [UInt32.max,1,2] : [0,0,0]
            var session = try await prepare(program, worker, OSDIPrecisionCoordinateQuotient(topology: program.topology, representatives: map))
            try await worker.source.close().wait(); var called = false
            do {
                if variant == 0 { try evaluate(&session, lower: [0,1,8], upper: [10,2,9]) { _ in called = true } }
                else if variant == 1 { try evaluate(&session, lower: [1,1,1], upper: [1,1,1],
                    lowerLow: [0,0x1p-54,0], upperLow: [0,0x1p-54,0]) { _ in called = true } }
                else { try evaluate(&session, lower: [1,-1,-1], upper: [2,1,1]) { _ in called = true } }
                Issue.record("Empty correlated condition published")
            } catch OSDIPrecisionConstitutiveError.emptyCoordinateCondition(let issue) { #expect(issue.workerStatus == 124) }
            #expect(!called)
            do { try evaluate(&session) { _ in called = true }; Issue.record("Failed session remained open") }
            catch OSDIPrecisionConstitutiveError.runtime(.closed) {}
            #expect(!called); try session.close()
        }
    }
    @Test func conditionedWorkIsCumulativeAndCancellationDoesNotPublish() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER")), program = try fixture(reference: worker.intent.expectedReference)
        let quotient = try OSDIPrecisionCoordinateQuotient(topology: program.topology, representatives: [0,0,2])
        var session = try await prepare(program, worker, quotient); try await worker.source.close().wait()
        let first = try evaluate(&session) { ($0.cover.completedWorkUnits, $0.cover.completedCopiedBytes) }
        let second = try evaluate(&session) { ($0.cover.completedWorkUnits, $0.cover.completedCopiedBytes) }
        #expect(second.0 > first.0 && second.1 > first.1); try session.close()
        let cappedWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        var capped = try await prepare(program, cappedWorker, quotient, work: second.0-1); try await cappedWorker.source.close().wait()
        try evaluate(&capped) { _ in }; var called = false
        do { try evaluate(&capped) { _ in called = true }; Issue.record("Conditioned work owner reset") }
        catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.workerStatus == 7) }
        #expect(!called); try capped.close()
        let task = Task {
            let cancelledWorker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var cancelled = try await prepare(program, cancelledWorker, quotient); try await cancelledWorker.source.close().wait()
            withUnsafeCurrentTask { $0?.cancel() }; var published = false
            do { try evaluate(&cancelled) { _ in published = true }; Issue.record("Cancelled condition published") }
            catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.code == .cancelled) }
            try cancelled.close(); return published
        }
        #expect(try await task.value == false)
    }
}
