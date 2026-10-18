import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_PRECISION_QUALIFICATION"] == "1", "Requires explicit OSDI_PRECISION_QUALIFICATION=1 and the real GMP/MPFR worker"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionRangeTests {
    private struct Fixtures: Decodable {
        struct Entry: Decodable {
            let id: String
            let programPath: String
            let setupPath: String
            let programSHA256: String
            let setupSHA256: String
            let rawNodeCount: Int
            let activeUnknownCount: Int
            let programHasTimeInput: Bool
        }
        let fixtures: [Entry]
    }

    private func external(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else {
            throw EvaluationFixtureError.requiredEnvironment(key)
        }
        return value
    }
    private func policy(work: UInt64 = 1_000_000, mapping: UInt64 = 1_048_576) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: mapping,
            arenaBytes: 1_048_576, maximumWorkUnits: work, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 1_000_000_000, evaluationTimeoutNanoseconds: 1_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func fixture(_ id: String, reference: ArtifactReference) throws -> OSDIPrecisionProgram {
        let path = URL(fileURLWithPath: try external("OSDI_RANGE_FIXTURES"), isDirectory: true).appendingPathComponent("manifest.json")
        let fixtures = try JSONDecoder().decode(Fixtures.self, from: Data(contentsOf: path))
        let entry = try #require(fixtures.fixtures.first { $0.id == id })
        let bytes = Array(try Data(contentsOf: URL(fileURLWithPath: entry.programPath)))
        let setup = Array(try Data(contentsOf: URL(fileURLWithPath: entry.setupPath)))
        let programDigest = try OSDIPrecisionProgramBuilder.digest(bytes)
        let setupDigest = try OSDIPrecisionProgramBuilder.digest(setup)
        try #require(programDigest.hexadecimalValue == entry.programSHA256 && setupDigest.hexadecimalValue == entry.setupSHA256)
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let nodes = try (0..<entry.rawNodeCount).map { index in
            try OSDINodeInfo(index: index, name: index == 0 ? "x" : "y", units: "V", residualUnits: "A", isFlow: false,
                unknownNature: nature, residualNature: nature, activeUnknownIndex: index)
        }
        let topology = try OSDIPreparedTopology(nodes: nodes, activeUnknownCount: entry.activeUnknownCount, stateCount: 0)
        return OSDIPrecisionProgram(modelReference: reference, descriptorIndex: 0, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: programDigest, setupDigest: setupDigest,
            semanticProfile: try ContentDigest(algorithm: .sha256, hexadecimalValue: String(repeating: "0", count: 64)))
    }
    private func numericProfile() throws -> ContentDigest {
        try ContentDigest(algorithm: .sha256, hexadecimalValue: external("OSDI_PRECISION_NUMERIC_PROFILE_SHA256"))
    }
    private func context(_ value: Double) throws -> OSDIPrecisionContext {
        try OSDIPrecisionContext(mode: .transient, timeHigh: value, timeLow: 0)
    }
    @Test func realWorkerEnclosesAbsoluteNonlinearValuesDerivativesAndTimeBox() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("polynomial", reference: worker.intent.expectedReference)
        let factory: any OSDIPrecisionRangeSessionPreparing = OSDINativePrecisionSessionPreparer()
        var session = try await factory.prepareRange(program: program, worker: worker.intent, source: worker.source,
            numericProfile: numericProfile(), options: policy())
        try await worker.source.close().wait()
        let evaluator: any OSDIPrecisionRangeEvaluating = OSDINativePrecisionEvaluator()
        let zero = [0.0]
        var address: UInt = 0
        for (sequence, bounds) in [(1, (1.0, 2.0, 0.25, 0.5)), (2, (0.5, 1.5, 0.0, 1.0)), (3, (-1.0, 2.0, 0.0, 0.5))] {
            let (a, b, t0, t1) = bounds
            let lower = [a], upper = [b]
            let squareLower = a <= 0 && b >= 0 ? 0 : min(a*a, b*b)
            let squareUpper = max(a*a, b*b)
            let observed = try evaluator.evaluateRange(session: &session,
                lowerHigh: lower.span, lowerLow: zero.span, upperHigh: upper.span, upperLow: zero.span,
                lowerContext: context(t0), upperContext: context(t1), precisionBits: 192) { view in
                #expect(view.fLower[0] <= squareLower+t0 && view.fUpper[0] >= squareUpper+t1)
                #expect(view.qLower[0] <= a*a*a+2*t0 && view.qUpper[0] >= b*b*b+2*t1)
                #expect(view.gLower[0] <= 2*a && view.gUpper[0] >= 2*b)
                #expect(view.cLower[0] <= 3*squareLower && view.cUpper[0] >= 3*squareUpper)
                #expect(view.fLower[0] < view.fUpper[0] && view.qLower[0] < view.qUpper[0])
                #expect(view.programHasTimeInput && view.requestSequence == UInt64(sequence))
                #expect(view.usedPrecisionBits == 192 && view.completedWorkUnits > 0)
                return view.fLower.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) }
            }
            if address != 0 { #expect(observed == address) }
            address = observed
        }
        try session.close(); try session.close()
        var called = false
        do {
            try evaluator.evaluateRange(session: &session, lowerHigh: zero.span, lowerLow: zero.span,
                upperHigh: zero.span, upperLow: zero.span, lowerContext: context(0), upperContext: context(0),
                precisionBits: 192) { _ in called = true }
            Issue.record("Closed range owner published a view")
        } catch OSDIError.closed {}
        #expect(!called)
    }

    @Test func exactTwofoldBoxAndPointOperationRemainDistinct() async throws {
        let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
        let program = try fixture("static_polynomial", reference: worker.intent.expectedReference)
        var session = try await OSDINativePrecisionSessionPreparer().prepareRange(program: program,
            worker: worker.intent, source: worker.source, numericProfile: numericProfile(), options: policy())
        try await worker.source.close().wait()
        let high = [1.0], lowerLow = [0x1p-55], upperLow = [0x1p-54], zero = [0.0]
        var called = false
        do {
            try OSDINativePrecisionEvaluator().evaluate(session: &session, candidateHigh: high.span,
                candidateLow: zero.span, referenceHigh: high.span, referenceLow: zero.span,
                candidateContext: context(0), referenceContext: context(0), precisionBits: 192) { _ in called = true }
            Issue.record("Point evaluator accepted a range owner")
        } catch OSDIError.invalidRange {}
        #expect(!called)
        try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
            lowerHigh: high.span, lowerLow: lowerLow.span, upperHigh: high.span, upperLow: upperLow.span,
            lowerContext: context(0), upperContext: context(0), precisionBits: 192) { view in
            #expect(view.requestSequence == 1 && !view.programHasTimeInput)
            #expect(view.fLower[0] == 1 && view.fUpper[0] == 1.0.nextUp)
            #expect(view.qLower[0] == 1 && view.qUpper[0] == 1.0.nextUp)
            #expect(view.gLower[0] == 2 && view.gUpper[0] == 2.0.nextUp)
            #expect(view.cLower[0] == 3 && view.cUpper[0] == 3.0.nextUp)
        }
        try session.close()
    }

    @Test func demandedSourceFacesRefuseAtomicallyWithTypedNonsmooth() async throws {
        #expect(OSDIPrecisionIssue.Code.nonsmooth.rawValue == 12 && OSDIPrecisionIssue.Code.io.rawValue == 10)
        let cases: [(String, [Double], [Double])] = [
            ("floor", [0.5], [1.5]), ("ceil", [1], [1.5]), ("cast", [0.25], [0.5]), ("trunc", [0.5], [1.5]),
            ("switch", [0.5], [1]), ("abs", [0], [1]), ("min", [1,1], [1,1]), ("max", [1,1], [1,1]),
            ("independent_equal", [1,1], [1,1]), ("sqrt", [0], [1]), ("log", [0], [1]), ("divide", [0], [1])]
        for (id, lower, upper) in cases {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var session = try await OSDINativePrecisionSessionPreparer().prepareRange(
                program: fixture(id, reference: worker.intent.expectedReference), worker: worker.intent,
                source: worker.source, numericProfile: numericProfile(), options: policy())
            try await worker.source.close().wait()
            let zero = Array(repeating: 0.0, count: lower.count)
            var called = false
            do {
                try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
                    lowerHigh: lower.span, lowerLow: zero.span, upperHigh: upper.span, upperLow: zero.span,
                    lowerContext: context(0), upperContext: context(0), precisionBits: 192) { _ in called = true }
                Issue.record("Demanded nonsmooth dependency published a view: \(id)")
            } catch OSDIError.precision(let issue) {
                #expect(issue.code == .nonsmooth && issue.workerStatus == 10, "\(id): \(issue)")
            }
            #expect(!called, "\(id)")
            try session.close()
        }
    }

    @Test func invalidBoxesWorkCapacityAndCancellationPublishNoView() async throws {
        for failure in ["reversed", "unnormalized", "time", "work", "capacity"] {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            let program = try fixture("polynomial", reference: worker.intent.expectedReference)
            var called = false
            do {
                var session = try await OSDINativePrecisionSessionPreparer().prepareRange(program: program,
                    worker: worker.intent, source: worker.source, numericProfile: numericProfile(),
                    options: policy(work: failure == "work" ? 1 : 1_000_000, mapping: failure == "capacity" ? 1 : 1_048_576))
                let lower = [failure == "reversed" ? 2.0 : 1.0], upper = [failure == "reversed" ? 1.0 : 2.0]
                let low = [failure == "unnormalized" ? 0.5 : 0.0], zero = [0.0]
                do {
                    try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
                        lowerHigh: lower.span, lowerLow: low.span, upperHigh: upper.span, upperLow: zero.span,
                        lowerContext: context(failure == "time" ? 1 : 0), upperContext: context(0), precisionBits: 192) { _ in called = true }
                    Issue.record("Invalid range request published a view: \(failure)")
                } catch OSDIError.precision(let issue) {
                    #expect(issue.code == (failure == "work" ? .numericFailure : .invalidInput), "\(failure): \(issue)")
                    if failure == "work" { #expect(issue.workerStatus == 7) }
                }
                try session.close()
            } catch let error as OSDIError {
                guard failure == "capacity" || failure == "work" else { throw error }
                if case .precision(let issue) = error {
                    #expect(issue.code == (failure == "capacity" ? .resourceLimit : .numericFailure))
                } else { guard case .resourceLimit = error, failure == "capacity" else { throw error } }
            }
            #expect(!called)
            try await worker.source.close().wait()
        }
        let cancelled = Task {
            let worker = try EvaluationArtifact(external("OSDI_PRECISION_WORKER"))
            var session = try await OSDINativePrecisionSessionPreparer().prepareRange(
                program: fixture("polynomial", reference: worker.intent.expectedReference), worker: worker.intent,
                source: worker.source, numericProfile: numericProfile(), options: policy())
            try await worker.source.close().wait()
            withUnsafeCurrentTask { $0?.cancel() }
            let high = [1.0], zero = [0.0]
            var called = false
            do {
                try OSDINativePrecisionEvaluator().evaluateRange(session: &session,
                    lowerHigh: high.span, lowerLow: zero.span, upperHigh: high.span, upperLow: zero.span,
                    lowerContext: context(0), upperContext: context(0), precisionBits: 192) { _ in called = true }
                Issue.record("Cancelled range request published a view")
            } catch OSDIError.precision(let issue) { #expect(issue.code == .cancelled) }
            try session.close()
            return called
        }
        #expect(try await cancelled.value == false)
    }
}
