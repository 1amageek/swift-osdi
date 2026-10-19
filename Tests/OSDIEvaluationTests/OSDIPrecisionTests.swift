import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionTests {
    private func policy(startup: UInt64 = 200_000_000, evaluation: UInt64 = 100_000_000) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 1_048_576,
            arenaBytes: 1_048_576, maximumWorkUnits: 1_000_000, minimumPrecisionBits: 53, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: startup, evaluationTimeoutNanoseconds: evaluation,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else {
            throw EvaluationFixtureError.requiredEnvironment(key)
        }
        return value
    }
    private func transportProgram(mode: UInt8, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let node = try OSDINodeInfo(index: 0, name: "p", units: "V", residualUnits: "A", isFlow: false,
            unknownNature: nature, residualNature: nature, activeUnknownIndex: 0)
        let topology = try OSDIPreparedTopology(nodes: [node], activeUnknownCount: 1, stateCount: 0)
        let bytes = [mode], setup: [UInt8] = [0]
        let digest = try OSDIPrecisionProgramBuilder.digest(bytes)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: digest,
            setupDigest: try OSDIPrecisionProgramBuilder.digest(setup), semanticProfile: digest)
    }
    @Test func scopedTransportAndIdempotentClose() async throws {
        let artifact = try EvaluationArtifact(try await ProtocolFixtureCache.shared.fixtures().transportWorker.path)
        let program = try transportProgram(mode: 0, reference: artifact.intent.expectedReference)
        // This success fixture tests borrowed storage and close, not scheduler
        // latency. Failure fixtures below isolate startup from evaluation deadlines.
        var session = try await OSDINativePrecisionSessionPreparer().prepare(program: program,
            worker: artifact.intent, source: artifact.source, numericProfile: program.programDigest,
            options: policy(startup: 2_000_000_000, evaluation: 2_000_000_000))
        try await artifact.source.close().wait()
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 1, timeLow: 0)
        let high = [1.0], low = [0.0], reference = [0.0]
        var address: UInt = 0
        for sequence in 1...4 {
            let observed = try OSDINativePrecisionEvaluator().evaluate(session: &session,
                candidateHigh: high.span, candidateLow: low.span, referenceHigh: reference.span, referenceLow: low.span,
                candidateContext: context, referenceContext: context, precisionBits: 128) { view in
                #expect(view.deltaQHigh[0] == 1 && view.deltaQLow[0] == 0 && view.deltaQRadius[0] == 0)
                #expect(view.g[0] == 1 && view.c[0] == 2 && view.fHigh[0] > 0)
                #expect(view.requestSequence == UInt64(sequence))
                return view.fHigh.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) }
            }
            if address != 0 { #expect(address == observed) }
            address = observed
        }
        try session.close(); try session.close()
        #expect(throws: OSDIError.self) {
            try OSDINativePrecisionEvaluator().evaluate(session: &session,
                candidateHigh: high.span, candidateLow: low.span, referenceHigh: reference.span, referenceLow: low.span,
                candidateContext: context, referenceContext: context, precisionBits: 128) { _ in true }
        }
    }
    @Test func failedWorkersPublishNoViewAndRetainNoChild() async throws {
        for mode: UInt8 in [1,2,3,4,6,7,8,9,10] {
            let artifact = try EvaluationArtifact(try await ProtocolFixtureCache.shared.fixtures().transportWorker.path)
            let program = try transportProgram(mode: mode, reference: artifact.intent.expectedReference)
            let startup: UInt64 = mode == 2 ? 200_000_000 : 2_000_000_000
            do {
                var session = try await OSDINativePrecisionSessionPreparer().prepare(program: program,
                    worker: artifact.intent, source: artifact.source, numericProfile: program.programDigest,
                    options: policy(startup: startup))
                if mode == 3 { try await Task.sleep(for: .milliseconds(100)) }
                let high = [1.0], low = [0.0]
                let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 1, timeLow: 0)
                var called = false
                do {
                    _ = try OSDINativePrecisionEvaluator().evaluate(session: &session,
                        candidateHigh: high.span, candidateLow: low.span, referenceHigh: high.span, referenceLow: low.span,
                        candidateContext: context, referenceContext: context, precisionBits: 128) { _ in called = true }
                    Issue.record("Failure fixture published success: \(mode)")
                } catch OSDIError.precision(let issue) {
                    let details: Comment = "mode=\(mode), code=\(issue.code), exit=\(String(describing: issue.exitCode)), signal=\(String(describing: issue.signal))"
                    #expect(issue.code != .cleanupFailure, details)
                }
                #expect(!called, "mode=\(mode): failure must not publish a view")
                try session.close()
            } catch OSDIError.precision(let issue) {
                let details: Comment = "mode=\(mode), code=\(issue.code), exit=\(String(describing: issue.exitCode)), signal=\(String(describing: issue.signal))"
                #expect(mode == 1 || mode == 2, details)
                #expect(issue.code == (mode == 1 ? .resourceLimit : .timeout), details)
            }
            try await artifact.source.close().wait()
        }
    }
    @Test func invalidContextsAndResourcePoliciesFail() throws {
        #expect(throws: OSDIContractError.self) {
            try OSDIPrecisionContext(mode: .transientInitialization, timeHigh: 1, timeLow: 0)
        }
        #expect(throws: OSDIContractError.self) {
            try OSDIPrecisionContext(mode: .transient, timeHigh: 1, timeLow: 0.5)
        }
        #expect(throws: OSDIContractError.self) {
            try OSDIPrecisionOptions(maximumProgramBytes: 1, maximumMappingBytes: 1, arenaBytes: 1,
                maximumWorkUnits: 1, minimumPrecisionBits: 52, maximumPrecisionBits: 128,
                startupTimeoutNanoseconds: 1, evaluationTimeoutNanoseconds: 1,
                cleanupTimeoutNanoseconds: 1, pollNanoseconds: 1)
        }
    }
    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires explicit OSDI_MODEL_QUALIFICATION=1 and exact external compiler/model inputs")) func actualCompilerProgramPreservesCompoundDefaultsAndLowCoordinates() async throws {
        let artifact = try EvaluationArtifact(external("OSDI_PRECISION_MODEL"))
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let profile = try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_SEMANTIC_SHA256"))
        let library = try await artifact.load()
        let preparation = try OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
            connectedTerminalCount: 1, limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576,
                maximumDiagnosticCount: 16, maximumDiagnosticBytes: 1024, maximumStringBytes: 65_536))
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
        let factory = OSDINativePrecisionSessionPreparer()
        let program = try factory.snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: preparation, semanticProfile: profile, options: policy())
        #expect(!program.programBytes.isEmpty && !program.setupBytes.isEmpty)
        #expect(program.programDigest != program.setupDigest)
        var session = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
            numericProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256")), options: policy())
        try await worker.source.close().wait()
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 1, timeLow: 0)
        let high = [1.0], low = [1.0e-20], zero = [0.0]
        try OSDINativePrecisionEvaluator().evaluate(session: &session, candidateHigh: high.span, candidateLow: low.span,
            referenceHigh: high.span, referenceLow: zero.span, candidateContext: context, referenceContext: context,
            precisionBits: 256) { view in
            #expect(view.fHigh[0] == 2)
            #expect(abs(view.fLow[0] - 2e-20) < 1e-35)
            #expect(view.fRadius[0] >= 0 && view.fRadius[0] < 1e-35)
            #expect(abs(view.deltaQHigh[0] - 1e-9 * low[0]) <= view.deltaQRadius[0] + 1e-44)
            #expect(view.deltaQHigh[0] != 0 && view.deltaQRadius[0] < 1e-44)
            #expect(view.g[0] == 2 && view.c[0] == 1e-9)
        }
        try session.close()
        // Precision cleanup cannot consume or poison the independent ordinary owner.
        let previous: [Double] = []
        let ordinary = try OSDINativeModelEvaluator().evaluate(model: &model,
            unknowns: high.span, previousState: previous.span) { $0.f[0] }
        #expect(ordinary == 1)
        model.close(); try library.close()
    }

    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires explicit OSDI_MODEL_QUALIFICATION=1 and exact external compiler/model inputs")) func explicitScopedPrimitiveFactsRemainGivenAndOriginalModelSurvivesFailure() async throws {
        let artifact = try EvaluationArtifact(external("OSDI_PRECISION_MODEL"))
        let library = try await artifact.load()
        let profile = try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_SEMANTIC_SHA256"))
        for (scope, factor) in [(OSDIParameterScope.model, 0.5), (.instance, 0.25)] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let preparation = try OSDIPreparationOptions(parameters: [OSDIParameterAssignment(name: "$mfactor", scope: scope, value: .real(factor))],
                simulationParameters: [], temperatureKelvin: 300.15, connectedTerminalCount: 1,
                limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576, maximumDiagnosticCount: 16,
                    maximumDiagnosticBytes: 1024, maximumStringBytes: 65_536))
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
            let factory = OSDINativePrecisionSessionPreparer()
            let program = try factory.snapshot(library: library, descriptorIndex: 0, model: model,
                preparation: preparation, semanticProfile: profile, options: policy())
            var session = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
                numericProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256")), options: policy())
            let high = [1.0], low = [0.0], previous: [Double] = []
            let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 1, timeLow: 0)
            try OSDINativePrecisionEvaluator().evaluate(session: &session, candidateHigh: high.span, candidateLow: low.span,
                referenceHigh: high.span, referenceLow: low.span, candidateContext: context, referenceContext: context,
                precisionBits: 256) { view in #expect(view.fHigh[0] == 2 * factor) }
            try session.close()
            let exhausted = try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 1_048_576,
                arenaBytes: 1_048_576, maximumWorkUnits: 1, minimumPrecisionBits: 53, maximumPrecisionBits: 256,
                startupTimeoutNanoseconds: 200_000_000, evaluationTimeoutNanoseconds: 100_000_000,
                cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
            var failing = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
                numericProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256")), options: exhausted)
            do {
                _ = try OSDINativePrecisionEvaluator().evaluate(session: &failing, candidateHigh: high.span, candidateLow: low.span,
                    referenceHigh: high.span, referenceLow: low.span, candidateContext: context, referenceContext: context,
                    precisionBits: 256) { _ in true }
                Issue.record("Exhausted worker published a candidate")
            } catch OSDIError.precision(let issue) {
                #expect(issue.code == .numericFailure && issue.workerStatus == 7)
            }
            try failing.close()
            let ordinary = try OSDINativeModelEvaluator().evaluate(model: &model,
                unknowns: high.span, previousState: previous.span) { $0.f[0] }
            #expect(ordinary == factor)
            model.close(); try await worker.source.close().wait()
        }
        try library.close()
    }

}
