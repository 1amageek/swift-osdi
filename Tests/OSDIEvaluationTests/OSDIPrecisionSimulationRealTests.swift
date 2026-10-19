import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_SIMULATION_REAL_QUALIFICATION"] == "1",
    "Requires explicit typed-REAL fixtures, new/old real workers and cold snapshot library"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionSimulationRealTests {
    private enum Operation: Int, CaseIterable { case point = 1, range, chart, cover, identity }
    private struct Manifest: Decodable {
        struct Entry: Decodable { let id, programPath, setupPath, programSHA256, setupSHA256: String; let scalar, expectedF: Double?; let semanticProfileSHA256: String? }
        let semanticProfileSHA256, coldProgramSHA256, rawGivenProgramSHA256, rawGivenSemanticProfileSHA256: String
        let fixtures: [Entry]
    }
    private struct Observation { let lower, upper: Double; let low: Double?; let claims: Int; let work: UInt64 }
    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else { throw EvaluationFixtureError.requiredEnvironment(key) }
        return value
    }
    private func manifest() throws -> Manifest {
        let url = URL(fileURLWithPath: try external("OSDI_SIMULATION_REAL_FIXTURES")).appendingPathComponent("manifest.json")
        return try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: url))
    }
    private func profile() throws -> ContentDigest {
        try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_SIMULATION_REAL_NUMERIC_PROFILE_SHA256"))
    }
    private func policy() throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 16_777_216,
            arenaBytes: 16_777_216, maximumWorkUnits: 10_000_000, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func coverLimits() throws -> OSDIPrecisionDecisionCoverLimits {
        try OSDIPrecisionDecisionCoverLimits(maximumSteps: 100_000, maximumGuardRecords: 100_000,
            maximumCopiedBytes: 10_000_000, maximumCells: 32, maximumNodes: 128, maximumLeaves: 32,
            maximumPending: 128, maximumDepth: 128, maximumIntegerBits: 1025)
    }
    private func fixture(_ id: String, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let description = try manifest()
        let found = description.fixtures.first { $0.id == id }
        let entry = try #require(found)
        let bytes = Array(try Data(contentsOf: URL(fileURLWithPath: entry.programPath)))
        let setup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
        let digest = try OSDIPrecisionProgramBuilder.digest(bytes), setupDigest = try OSDIPrecisionProgramBuilder.digest(setup)
        try #require(digest.hexadecimalValue == entry.programSHA256)
        try #require(setupDigest.hexadecimalValue == entry.setupSHA256)
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let nodes = try ["x", "y"].enumerated().map { index, name in
            try OSDINodeInfo(index: index, name: name, units: "V", residualUnits: "A", isFlow: false,
                unknownNature: nature, residualNature: nature, activeUnknownIndex: index)
        }
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0,
            topology: try OSDIPreparedTopology(nodes: nodes, activeUnknownCount: 2, stateCount: 0),
            programBytes: bytes, setupBytes: setup, programDigest: digest, setupDigest: setupDigest,
            semanticProfile: try ContentDigest(algorithm: .sha256, hexadecimalValue: entry.semanticProfileSHA256 ?? description.semanticProfileSHA256))
    }
    private func prepare(_ operation: Operation, program: OSDIPrecisionProgram,
        worker: EvaluationArtifact, numeric: ContentDigest? = nil) async throws -> OSDIPrecisionSession {
        let factory = OSDINativePrecisionSessionPreparer()
        let selectedProfile = try numeric ?? profile()
        switch operation {
        case .point: return try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
            numericProfile: selectedProfile, options: policy())
        case .range: return try await factory.prepareRange(program: program, worker: worker.intent, source: worker.source,
            numericProfile: selectedProfile, options: policy())
        case .chart: return try await factory.prepareChart(program: program, worker: worker.intent, source: worker.source,
            numericProfile: selectedProfile, options: policy(),
            limits: OSDIPrecisionChartLimits(maximumSteps: 100_000, maximumTapeEntries: 100_000, maximumGuardRecords: 100_000))
        case .cover: return try await factory.prepareDecisionCover(program: program, worker: worker.intent, source: worker.source,
            numericProfile: selectedProfile, options: policy(), limits: coverLimits())
        case .identity: return try await factory.prepareConstitutiveIdentities(program: program, worker: worker.intent, source: worker.source,
            numericProfile: selectedProfile, options: policy(), coverLimits: coverLimits(),
            limits: OSDIPrecisionConstitutiveLimits(maximumNodes: 256, maximumAtoms: 64,
                maximumCoefficientBits: 512, maximumWorkspaceBytes: 2_097_152),
            coordinateQuotient: .identity(topology: program.topology))
        }
    }
    private func evaluate(_ operation: Operation, session: inout OSDIPrecisionSession) throws -> Observation {
        let high = [1.0, 2.0], low = [0.0, 0.0]
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0)
        let evaluator = OSDINativePrecisionEvaluator()
        switch operation {
        case .point:
            return try evaluator.evaluate(session: &session, candidateHigh: high.span, candidateLow: low.span,
                referenceHigh: high.span, referenceLow: low.span, candidateContext: context, referenceContext: context,
                precisionBits: 192) { view in
                    return Observation(lower: view.fHigh[0], upper: view.fHigh[0], low: view.fLow[0], claims: 0, work: view.completedWorkUnits)
                }
        case .range:
            return try evaluator.evaluateRange(session: &session, lowerHigh: high.span, lowerLow: low.span,
                upperHigh: high.span, upperLow: low.span, lowerContext: context, upperContext: context,
                precisionBits: 192) { Observation(lower: $0.fLower[0], upper: $0.fUpper[0], low: nil, claims: 0, work: $0.completedWorkUnits) }
        case .chart:
            return try evaluator.evaluateChart(session: &session, lowerHigh: high.span, lowerLow: low.span,
                upperHigh: high.span, upperLow: low.span, lowerContext: context, upperContext: context,
                precisionBits: 192, mode: .record, tape: nil) { view in
                    #expect(view.guards.count > 0)
                    return Observation(lower: view.fLower[0], upper: view.fUpper[0], low: nil, claims: 0, work: view.completedWorkUnits)
                }
        case .cover:
            return try evaluator.constructDecisionCover(session: &session, lowerHigh: high.span, lowerLow: low.span,
                upperHigh: high.span, upperLow: low.span, lowerContext: context, upperContext: context, precisionBits: 192) { view in
                    #expect(view.leafCount == 1)
                    return view.withLeaf(at: 0) { Observation(lower: $0.fLower[0], upper: $0.fUpper[0], low: nil, claims: 0, work: view.completedWorkUnits) }
                }
        case .identity:
            return try evaluator.constructConstitutiveIdentities(session: &session, lowerHigh: high.span, lowerLow: low.span,
                upperHigh: high.span, upperLow: low.span, lowerContext: context, upperContext: context, precisionBits: 192) { view in
                    #expect(view.cover.leafCount == 1)
                    var f = 0, q = 0
                    for index in 0..<view.claimCount { view.withClaim(at: index) { claim in
                        #expect(claim.rawRowCount == 2)
                        if claim.kind == .resistive { f += 1 } else { q += 1 }
                    }}
                    // F/Q=(a*x,x) has an exact dyadic relation only when the setup
                    // scalar is source-literal authority, not an opaque atom or midpoint.
                    #expect(f == 1 && q == 1)
                    return view.cover.withLeaf(at: 0) { Observation(lower: $0.fLower[0], upper: $0.fUpper[0], low: nil,
                        claims: view.claimCount, work: view.cover.completedWorkUnits) }
                }
        }
    }
    private func issue(_ error: any Error) throws -> OSDIPrecisionIssue {
        if let runtime = error as? OSDIError, case .precision(let value) = runtime { return value }
        if let chart = error as? OSDIPrecisionChartError, case .runtime(let runtime) = chart { return try issue(runtime) }
        if let cover = error as? OSDIPrecisionDecisionCoverError, case .runtime(let runtime) = cover { return try issue(runtime) }
        if let identity = error as? OSDIPrecisionConstitutiveError {
            switch identity {
            case .runtime(let runtime): return try issue(runtime)
            case .cover(let cover): return try issue(cover)
            default: break
            }
        }
        throw error
    }

    @Test func allStagesAndAllFiveOperationsPreserveTypedFallbackAndExactLiterals() async throws {
        for operation in Operation.allCases {
            for id in ["missing", "string", "real", "tiny-real", "case-different", "nongiven-real"] {
                let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
                let program = try fixture(id, reference: worker.intent.expectedReference)
                let entries = try manifest().fixtures
                let found = entries.first { $0.id == id }?.scalar
                let scalar = try #require(found)
                let expected = 3 * scalar
                var session = try await prepare(operation, program: program, worker: worker)
                try await worker.source.close().wait()
                let first = try evaluate(operation, session: &session)
                let second = try evaluate(operation, session: &session)
                #expect(first.lower <= expected && first.upper >= expected)
                #expect(first.lower == second.lower && first.upper == second.upper)
                if operation == .point { #expect(first.low == (-expected).addingProduct(3, scalar)) }
                if operation.rawValue >= 3 { #expect(second.work > first.work) }
                try session.close(); try session.close()
            }
        }
    }

    @Test func explicitRealZeroDemandsOriginalFatalAcrossEveryOperation() async throws {
        for operation in Operation.allCases {
            let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
            let program = try fixture("real-zero", reference: worker.intent.expectedReference)
            var session = try await prepare(operation, program: program, worker: worker)
            try await worker.source.close().wait()
            do { _ = try evaluate(operation, session: &session); Issue.record("REAL zero silently selected the missing fallback") }
            catch {
                let failure = try issue(error)
                #expect(failure.code == .numericFailure)
                #expect(failure.workerStatus == 4)
            }
            try session.close()
        }
    }

    @Test func malformedBindingsDuplicateNamesAndLegacyTypeMismatchRefuse() async throws {
        for id in ["legacy-string", "duplicate", "bad-value-type", "bad-given-type",
                   "raw-given-invalid-type", "raw-given-invalid-index", "raw-given-invalid-aux", "raw-given-invalid-stage"] {
            let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
            let program = try fixture(id, reference: worker.intent.expectedReference)
            do {
                var session = try await prepare(.point, program: program, worker: worker)
                try session.close(); Issue.record("Invalid program admitted: \(id)")
            } catch {
                let failure = try issue(error)
                #expect(failure.code == .numericFailure)
                #expect(failure.workerStatus == 2)
            }
            try await worker.source.close().wait()
        }
        for (id, expected) in [("legacy-real", 6.0), ("legacy-presence-string", 1.0), ("typed-presence-string", 0.0)] {
            let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
            let program = try fixture(id, reference: worker.intent.expectedReference)
            var session = try await prepare(.point, program: program, worker: worker)
            try await worker.source.close().wait()
            let observation = try evaluate(.point, session: &session)
            #expect(observation.lower == expected)
            try session.close()
        }
    }

    @Test func releasedOldWorkerRefusesNewKindsAndStillRunsLegacyGrammar() async throws {
        let oldProfile = try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_OLD_NUMERIC_PROFILE_SHA256"))
        for id in ["missing", "raw-given-model-only", "legacy-real"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_OLD_WORKER"))
            let program = try fixture(id, reference: worker.intent.expectedReference)
            do {
                var session = try await prepare(.point, program: program, worker: worker, numeric: oldProfile)
                let result = try evaluate(.point, session: &session)
                #expect(id == "legacy-real" && result.lower == 6)
                try session.close()
            } catch {
                #expect(id != "legacy-real")
                let failure = try issue(error)
                #expect(failure.code == .numericFailure && failure.workerStatus == 2)
            }
            try await worker.source.close().wait()
        }
    }

    @Test func rawScopedGivenAndLegacyMergedGivenRemainDistinctAcrossAllOperations() async throws {
        let entries = try manifest().fixtures
        for operation in Operation.allCases {
            for id in ["raw-given-model-only", "raw-given-instance-same", "raw-given-instance-zero", "raw-given-neither"] {
                let expected = try #require(entries.first { $0.id == id }?.expectedF)
                let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
                let program = try fixture(id, reference: worker.intent.expectedReference)
                var session = try await prepare(operation, program: program, worker: worker)
                try await worker.source.close().wait()
                let first = try evaluate(operation, session: &session)
                let second = try evaluate(operation, session: &session)
                // The source keeps both predicates: model-only has merged=true,
                // own=false and F=3*x; equal or zero explicit instance values have
                // merged=true, own=true and F=5*x. No resolved value implies given.
                #expect(first.lower == expected && first.upper == expected)
                #expect(second.lower == first.lower && second.upper == first.upper)
                try session.close(); try session.close()
            }
        }
    }

    private func u32(_ bytes: [UInt8], _ start: Int) -> UInt32 {
        var value: UInt32 = 0
        for index in 0..<4 { value |= UInt32(bytes[start + index]) << (8 * index) }
        return value
    }
    private func u64(_ bytes: [UInt8], _ start: Int) -> UInt64 {
        UInt64(u32(bytes, start)) | UInt64(u32(bytes, start + 4)) << 32
    }
    @Test func coldSnapshotRetainsStringAndRealZeroAsDistinctImmutableFacts() async throws {
        let artifact = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_LIBRARY"))
        let library = try await artifact.load()
        let description = try manifest()
        let semantic = try ContentDigest(algorithm: .sha256, hexadecimalValue: description.semanticProfileSHA256)
        var snapshots: [OSDIPrecisionProgram] = []
        let variants: [[OSDISimulationParameter]] = [[], [try OSDISimulationParameter(name: "tnom", value: .string("x"))],
            [try OSDISimulationParameter(name: "tnom", value: .real(0))]]
        for facts in variants {
            let preparation = try OSDIPreparationOptions(parameters: [], simulationParameters: facts, temperatureKelvin: 300.15,
                connectedTerminalCount: 1, limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576,
                    maximumDiagnosticCount: 16, maximumDiagnosticBytes: 1024, maximumStringBytes: 65_536))
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
            let snapshot = try OSDINativePrecisionSessionPreparer().snapshot(library: library, descriptorIndex: 0, model: model,
                preparation: preparation, semanticProfile: semantic, options: policy())
            let repeated = try OSDINativePrecisionSessionPreparer().snapshot(library: library, descriptorIndex: 0, model: model,
                preparation: preparation, semanticProfile: semantic, options: policy())
            #expect(snapshot.programBytes == repeated.programBytes && snapshot.setupBytes == repeated.setupBytes)
            snapshots.append(snapshot); model.close()
        }
        try library.close(); try await artifact.source.close().wait()
        #expect(snapshots[0].programBytes == snapshots[1].programBytes && snapshots[1].programBytes == snapshots[2].programBytes)
        #expect(snapshots[0].setupDigest != snapshots[1].setupDigest && snapshots[1].setupDigest != snapshots[2].setupDigest)
        #expect(u32(snapshots[0].setupBytes, 56) == 0)
        for index in 1...2 {
            let bytes = snapshots[index].setupBytes, offset = Int(u64(snapshots[index].setupBytes, 48))
            #expect(u32(bytes, 56) == 1 && u32(bytes, offset + 12) == 1)
            #expect(u32(bytes, offset + 8) == (index == 1 ? 4 : 1))
            if index == 1 {
                let bits = u64(bytes, offset + 16), pool = Int(u64(bytes, 80))
                #expect(bits >> 32 == 1 && bytes[pool + Int(UInt32(truncatingIfNeeded: bits))] == 120)
            } else { #expect(u64(bytes, offset + 16) == 0) }
        }
        #expect(snapshots[0].programDigest.hexadecimalValue == description.coldProgramSHA256)
        let high = [1.0], low = [0.0]
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0)
        for snapshot in snapshots {
            let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
            var session = try await prepare(.point, program: snapshot, worker: worker)
            try await worker.source.close().wait()
            try OSDINativePrecisionEvaluator().evaluate(session: &session, candidateHigh: high.span, candidateLow: low.span,
                referenceHigh: high.span, referenceLow: low.span, candidateContext: context, referenceContext: context,
                precisionBits: 192) { view in
                    #expect(view.fHigh[0] == 1 && view.fLow[0] == 0 && view.g[0] == 1)
                    #expect(view.deltaQHigh[0] == 0 && view.deltaQRadius[0] == 0)
                }
            try session.close()
        }
    }

    @Test func coldSnapshotPreservesOwnScopeFactsAndOriginalGivenCallbacks() async throws {
        let artifact = try EvaluationArtifact(external("OSDI_RAW_GIVEN_LIBRARY"))
        let library = try await artifact.load()
        let description = try manifest()
        let semantic = try ContentDigest(algorithm: .sha256, hexadecimalValue: description.rawGivenSemanticProfileSHA256)
        let ids = ["raw-given-model-only", "raw-given-instance-same", "raw-given-instance-zero", "raw-given-neither"]
        var snapshots: [(OSDIPrecisionProgram, Double)] = []
        for (index, id) in ids.enumerated() {
            var assignments: [OSDIParameterAssignment] = []
            if index < 3 { assignments.append(try OSDIParameterAssignment(name: "gain", scope: .model, value: .real(1))) }
            if index == 1 || index == 2 {
                assignments.append(try OSDIParameterAssignment(name: "gain", scope: .instance, value: .real(index == 1 ? 1 : 0)))
            }
            let preparation = try OSDIPreparationOptions(parameters: assignments, simulationParameters: [], temperatureKelvin: 300.15,
                connectedTerminalCount: 2, limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576,
                    maximumDiagnosticCount: 16, maximumDiagnosticBytes: 1024, maximumStringBytes: 65_536))
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
            let snapshot = try OSDINativePrecisionSessionPreparer().snapshot(library: library, descriptorIndex: 0, model: model,
                preparation: preparation, semanticProfile: semantic, options: policy())
            let entry = try #require(description.fixtures.first { $0.id == id })
            let expected = try #require(entry.expectedF)
            let capturedSetup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
            #expect(snapshot.programDigest.hexadecimalValue == description.rawGivenProgramSHA256)
            #expect(snapshot.setupBytes == capturedSetup)
            let factOffset = Int(u64(snapshot.setupBytes, 32))
            #expect(u32(snapshot.setupBytes, 40) == 2)
            #expect(u32(snapshot.setupBytes, factOffset + 4) == (index < 3 ? 1 : 0))
            #expect(u32(snapshot.setupBytes, factOffset + 20) == (index == 1 || index == 2 ? 1 : 0))
            let high = [1.0, 2.0], previous: [Double] = []
            let ordinary = try OSDINativeModelEvaluator().evaluate(model: &model,
                unknowns: high.span, previousState: previous.span) { $0.f[0] }
            #expect(ordinary == expected)
            snapshots.append((snapshot, expected)); model.close()
        }
        try library.close(); try await artifact.source.close().wait()
        for (snapshot, expected) in snapshots {
            for operation in Operation.allCases {
                let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
                var session = try await prepare(operation, program: snapshot, worker: worker)
                try await worker.source.close().wait()
                let observed = try evaluate(operation, session: &session)
                #expect(observed.lower == expected && observed.upper == expected)
                try session.close()
            }
        }
    }

    @Test func cancelledTypedIdentityAttemptClosesWithoutPublication() async throws {
        for id in ["string", "raw-given-model-only"] {
            let task = Task {
                let worker = try EvaluationArtifact(external("OSDI_SIMULATION_REAL_WORKER"))
                let program = try fixture(id, reference: worker.intent.expectedReference)
                var session = try await prepare(.identity, program: program, worker: worker)
                try await worker.source.close().wait()
                withUnsafeCurrentTask { $0?.cancel() }
                do { _ = try evaluate(.identity, session: &session); Issue.record("Cancelled typed identity published") }
                catch { let failure = try issue(error); #expect(failure.code == .cancelled) }
                try session.close(); return true
            }
            #expect(try await task.value)
        }
    }
}
