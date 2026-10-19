import CircuiteFoundation
import CircuiteFoundationCrypto
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Testing

@Suite(.enabled(if: ProcessInfo.processInfo.environment["OSDI_CONSTITUTIVE_ACTUAL_QUALIFICATION"] == "1", "Requires pinned original four-family captures and independent C identity receipts"), .serialized, .timeLimit(.minutes(1)))
struct OSDIPrecisionConstitutiveActualModelTests {
    private struct Pin: Decodable {
        let path: String
        let sha256: String
        let bytes: Int?
        func read() throws -> Data {
            let data = try Data(contentsOf: URL(fileURLWithPath: path))
            try #require(bytes == nil || bytes == data.count)
            try #require(OSDIPrecisionConstitutiveActualModelTests.digest(data) == sha256, "Evidence changed: \(path)")
            return data
        }
    }
    private struct Manifest: Decodable {
        struct Entry: Decodable {
            let id, sourceFamily, program, setup, request, requestFormat: String
            let programSHA256, setupSHA256, requestSHA256: String
            let rawNodeCount, activeUnknownCount: Int
            let originalPointRequest, sourceInputManifest, priorCoverReceipt, priorQualification: Pin
        }
        let schema, baseSDKRevision: String
        let originalSourceInputPins: [Pin]
        let cases: [Entry]
    }
    private struct SourceManifest: Decodable {
        struct Family: Decodable {
            struct Context: Decodable { let timeHighBits, timeLowBits: UInt64 }
            let family, programSHA256, setupSHA256, requestSHA256, pointRequestSHA256: String
            let rawNodeCount, activeUnknownCount: Int
            let centerHighBits, centerLowBits: [UInt64]
            let context: Context
            let precisionBits: UInt32
        }
        let schema: String
        let controlBytes: Int
        let inputs: [Pin]
        let families: [Family]
    }
    private struct CoverReceipt: Decodable { let status: String; let exitCode: Int; let command: [String] }
    private struct CoverQualification: Decodable { let status: String; let receipts: [Pin] }
    private struct CProof: Decodable {
        struct Limits: Decodable {
            let coefficient_bits, arena_bytes, workspace_bytes, work, steps: UInt64
            let maximum_atoms, maximum_nodes: UInt32
        }
        struct Entry: Decodable {
            struct Result: Decodable {
                struct Identity: Decodable { let kind: UInt32; let rawRowCoefficients: [String] }
                let status, claimCount, rawRows, terminalCount: Int
                let identities: [Identity]
            }
            let id: String
            let inputs: [Pin]
            let result: Result
        }
        let schema: String
        let synthetic: String
        let rootFlowHistoryAuthority: Bool
        let limits: Limits
        let binary: Pin
        let actualCases: [Entry]
    }
    private struct PointRequest: Decodable {
        struct Model: Decodable {
            struct SimulationParameter: Decodable { let name: String; let value: EvaluationParameterValue }
            struct Limits: Decodable {
                let maximumWorkspaceBytes, maximumDiagnosticBytes, maximumStringBytes: UInt64
                let maximumDiagnosticCount: UInt32
            }
            let libraryPath, expectedSHA256: String
            let expectedByteCount, descriptorIndex, connectedTerminalCount: Int
            let temperatureKelvin: Double
            let parameters: [EvaluationFixtureRequest.Parameter]
            let simulationParameters: [SimulationParameter]
            let evaluationLimits: Limits
            func preparation() throws -> OSDIPreparationOptions {
                try OSDIPreparationOptions(parameters: parameters.map { try $0.assignment() },
                    simulationParameters: simulationParameters.map { try OSDISimulationParameter(name: $0.name, value: $0.value.value()) },
                    temperatureKelvin: temperatureKelvin, connectedTerminalCount: connectedTerminalCount,
                    limits: OSDIEvaluationLimits(maximumWorkspaceBytes: evaluationLimits.maximumWorkspaceBytes,
                        maximumDiagnosticCount: evaluationLimits.maximumDiagnosticCount,
                        maximumDiagnosticBytes: evaluationLimits.maximumDiagnosticBytes, maximumStringBytes: evaluationLimits.maximumStringBytes))
            }
        }
        let model: Model
        let semanticProfileSHA256: String
    }
    private struct Capture {
        let lowerHigh, lowerLow, upperHigh, upperLow: [Double]
        let lowerContext, upperContext: OSDIPrecisionContext
        let precision, minimumPrecision, maximumPrecision: UInt32
    }
    private static func environment(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else { throw EvaluationFixtureError.requiredEnvironment(key) }
        return value
    }
    private static func digest(_ data: Data) throws -> String {
        try SHA256ContentDigester().digest(using: .sha256, limits: .singleBufferMaximum) { lease throws(ContentDigestError) in
            try lease.update(Array(data))
        }.digest.hexadecimalValue
    }
    private func capture(_ data: Data, entry: Manifest.Entry, family: SourceManifest.Family) throws -> Capture {
        try #require(data.count == 464 + 4*entry.activeUnknownCount*8 && data.count % 8 == 0)
        func u32(_ offset: Int) -> UInt32 { data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self).littleEndian } }
        func u64(_ offset: Int) -> UInt64 { data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt64.self).littleEndian } }
        try #require(u64(0) == 0x313056504944534f && u32(8) == 2 && u32(12) == 464 && u32(24) == 0x01020304)
        try #require(u32(28) == 0 && u64(32) == entry.activeUnknownCount && u64(40) == entry.rawNodeCount)
        try #require(u64(16) == 0 && data[128..<352].allSatisfy { $0 == 0 })
        try #require(data[64..<96].map { String(format: "%02x", $0) }.joined() == entry.programSHA256)
        try #require(u32(60) >= 53 && u32(60) <= u32(416) && u32(416) <= u32(56) && u32(416) == family.precisionBits)
        let n = entry.activeUnknownCount
        let bitPlanes = (0..<4).map { plane in (0..<n).map { u64(464 + (plane*n+$0)*8) } }
        try #require(bitPlanes == [family.centerHighBits, family.centerLowBits, family.centerHighBits, family.centerLowBits])
        func context(_ offset: Int) throws -> OSDIPrecisionContext {
            try #require(u32(offset) == 2 && u32(offset+4) == 0)
            try #require(u64(offset+8) == family.context.timeHighBits && u64(offset+16) == family.context.timeLowBits)
            return try OSDIPrecisionContext(mode: .transient, timeHigh: Double(bitPattern: u64(offset+8)), timeLow: Double(bitPattern: u64(offset+16)))
        }
        return try Capture(lowerHigh: bitPlanes[0].map(Double.init(bitPattern:)), lowerLow: bitPlanes[1].map(Double.init(bitPattern:)),
            upperHigh: bitPlanes[2].map(Double.init(bitPattern:)), upperLow: bitPlanes[3].map(Double.init(bitPattern:)),
            lowerContext: context(368), upperContext: context(392), precision: u32(416), minimumPrecision: u32(60), maximumPrecision: u32(56))
    }
    private func policy(_ proof: CProof, capture: Capture, work: UInt64? = nil) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 16_777_216, maximumMappingBytes: 268_435_456,
            arenaBytes: proof.limits.arena_bytes, maximumWorkUnits: work ?? proof.limits.work,
            minimumPrecisionBits: capture.minimumPrecision, maximumPrecisionBits: capture.maximumPrecision,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 5_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func coverLimits(_ proof: CProof) throws -> OSDIPrecisionDecisionCoverLimits {
        try OSDIPrecisionDecisionCoverLimits(maximumSteps: proof.limits.steps, maximumGuardRecords: proof.limits.steps,
            maximumCopiedBytes: 268_435_456, maximumCells: 100, maximumNodes: 20_000, maximumLeaves: 64,
            maximumPending: 20_000, maximumDepth: 20_000, maximumIntegerBits: 1025)
    }
    private func identityLimits(_ proof: CProof) throws -> OSDIPrecisionConstitutiveLimits {
        try OSDIPrecisionConstitutiveLimits(maximumNodes: proof.limits.maximum_nodes, maximumAtoms: proof.limits.maximum_atoms,
            maximumCoefficientBits: proof.limits.coefficient_bits, maximumWorkspaceBytes: proof.limits.workspace_bytes)
    }
    private func evaluate<Result>(_ session: inout OSDIPrecisionSession, capture: Capture,
        _ body: (OSDIPrecisionConstitutiveEvaluationView) -> Result) throws -> Result {
        try OSDINativePrecisionEvaluator().constructConstitutiveIdentities(session: &session,
            lowerHigh: capture.lowerHigh.span, lowerLow: capture.lowerLow.span, upperHigh: capture.upperHigh.span, upperLow: capture.upperLow.span,
            lowerContext: capture.lowerContext, upperContext: capture.upperContext, precisionBits: capture.precision, body)
    }
    private func compare(_ view: borrowing OSDIPrecisionConstitutiveEvaluationView, expected: CProof.Entry.Result,
        program: OSDIPrecisionProgram, capture: Capture) {
        let binding = view.cover.binding, leafCount = view.cover.leafCount
        let precision = view.cover.usedPrecisionBits, claimCount = view.claimCount
        let lowerContext = view.cover.lowerContext, upperContext = view.cover.upperContext
        let condition = view.binding.coordinateQuotient.representatives
        #expect(view.binding.source == binding && condition == (0..<program.topology.activeUnknownCount).map(UInt32.init))
        #expect(binding == OSDIPrecisionChartBinding(program: program) && leafCount == expected.terminalCount)
        #expect(precision == capture.precision && claimCount == expected.claimCount)
        #expect(lowerContext == capture.lowerContext && upperContext == capture.upperContext)
        for i in 0..<capture.lowerHigh.count {
            let lowerHigh = view.cover.lowerHigh[i].bitPattern, lowerLow = view.cover.lowerLow[i].bitPattern
            let upperHigh = view.cover.upperHigh[i].bitPattern, upperLow = view.cover.upperLow[i].bitPattern
            #expect(lowerHigh == capture.lowerHigh[i].bitPattern && lowerLow == capture.lowerLow[i].bitPattern)
            #expect(upperHigh == capture.upperHigh[i].bitPattern && upperLow == capture.upperLow[i].bitPattern)
        }
        guard claimCount == expected.identities.count else { return }
        for i in 0..<claimCount { view.withClaim(at: i) { claim in
            let claimBinding = claim.binding
            #expect(claimBinding == view.binding)
            let reference = expected.identities[i]
            let kind = claim.kind.rawValue, rowCount = claim.rawRowCount
            #expect(kind == reference.kind && rowCount == expected.rawRows)
            guard rowCount == reference.rawRowCoefficients.count else { return }
            for row in 0..<rowCount { claim.withCoefficient(at: row) { value in
                // These four retained independent receipts have only -1/0/+1;
                // require exact limbs/sign/power, without a floating conversion.
                let scalar = reference.rawRowCoefficients[row]
                let power = value.denominatorPower, negative = value.negative
                let wordCount = value.numeratorWords.count
                let firstWord: UInt64? = wordCount == 0 ? nil : value.numeratorWords[0]
                #expect(["-1", "0", "1"].contains(scalar) && power == 0)
                #expect(negative == (scalar == "-1"))
                if scalar == "0" { #expect(wordCount == 0) }
                else { #expect(wordCount == 1 && firstWord == 1) }
            }}
        }}
    }
    @Test func fourOriginalModelCapturesJoinExactCheckedKernelAndCumulativeOwner() async throws {
        let manifestPin = Pin(path: try Self.environment("OSDI_CONSTITUTIVE_ACTUAL_MANIFEST"),
            sha256: try Self.environment("OSDI_CONSTITUTIVE_ACTUAL_MANIFEST_SHA256"), bytes: nil)
        let proofPin = Pin(path: try Self.environment("OSDI_CONSTITUTIVE_C_PROOF_RECEIPT"),
            sha256: try Self.environment("OSDI_CONSTITUTIVE_C_PROOF_RECEIPT_SHA256"), bytes: nil)
        let decoder = JSONDecoder()
        let manifest = try decoder.decode(Manifest.self, from: manifestPin.read())
        let proof = try decoder.decode(CProof.self, from: proofPin.read())
        try #require(manifest.schema == "osdi.constitutive-identities-source-binding.v1" && manifest.baseSDKRevision == "d10443fb6c1445c986dc35e64030588e77e432a2")
        try #require(proof.schema == "osdi.constitutive-identities-verification.v1" && proof.synthetic == "passed" && !proof.rootFlowHistoryAuthority)
        try #require(manifest.cases.count == 4 && Set(manifest.cases.map(\.id)) == ["bulk", "cmg", "img", "soi"])
        try #require(proof.actualCases.count == 4 && Set(proof.actualCases.map(\.id)) == Set(manifest.cases.map(\.id)))
        _ = try proof.binary.read()
        // These hashes describe the historical capture producer, not current
        // source compatibility. The entire original manifest remains pinned.
        let historicalProducerSuffixes = ["/Sources/COSDI/include/OSDIPrecisionProtocol.h",
            "/Tools/OSDIPrecisionVerification/chart_requests.py", "/Tools/OSDIPrecisionVerification/verify_worker.py"]
        for pin in manifest.originalSourceInputPins where !historicalProducerSuffixes.contains(where: pin.path.hasSuffix) {
            if pin.path.hasSuffix("/BSIM-BULK-swift/Tools/BSIMBulkGeneration/Upstream/code/bsimbulk.va") {
                // Retirement changes availability only. Require the explicitly
                // selected archive bytes to retain the original source digest.
                _ = try Pin(path: Self.environment("OSDI_CONSTITUTIVE_BULK_SOURCE"), sha256: pin.sha256, bytes: pin.bytes).read()
            } else { _ = try pin.read() }
        }
        for entry in manifest.cases {
            let original = try decoder.decode(SourceManifest.self, from: entry.sourceInputManifest.read())
            try #require(original.schema == "osdi.internal-chart-singleton-requests.v1" && original.controlBytes == 464)
            try #require(original.inputs.map(\.sha256) == manifest.originalSourceInputPins.map(\.sha256))
            try #require(original.inputs.map(\.path) == manifest.originalSourceInputPins.map(\.path))
            let family = try #require(original.families.first { $0.family == entry.sourceFamily })
            try #require(family.programSHA256 == entry.programSHA256 && family.setupSHA256 == entry.setupSHA256 && family.requestSHA256 == entry.requestSHA256)
            try #require(family.rawNodeCount == entry.rawNodeCount && family.activeUnknownCount == entry.activeUnknownCount)
            let prior = try decoder.decode(CoverReceipt.self, from: entry.priorCoverReceipt.read())
            let qualified = try decoder.decode(CoverQualification.self, from: entry.priorQualification.read())
            try #require(prior.status == "passed" && prior.exitCode == 0 && prior.command.count > 4 && qualified.status == "passed")
            try #require(Array(prior.command[2...4]) == [entry.program, entry.setup, entry.request])
            try #require(qualified.receipts.contains { $0.sha256 == entry.priorCoverReceipt.sha256 })
            let reference = try #require(proof.actualCases.first { $0.id == entry.id })
            try #require(reference.result.status == 0 && reference.result.terminalCount == 1)
            try #require(reference.result.rawRows == entry.rawNodeCount && reference.result.claimCount == reference.result.identities.count)
            let requiredPins = [(entry.program, entry.programSHA256), (entry.setup, entry.setupSHA256), (entry.request, entry.requestSHA256)]
            for (path, sha) in requiredPins { try #require(reference.inputs.contains { $0.path == path && $0.sha256 == sha }) }
            let programData = try Pin(path: entry.program, sha256: entry.programSHA256, bytes: nil).read()
            let setupData = try Pin(path: entry.setup, sha256: entry.setupSHA256, bytes: nil).read()
            let captured = try capture(Pin(path: entry.request, sha256: entry.requestSHA256, bytes: nil).read(), entry: entry, family: family)
            try #require(entry.requestFormat == "control-v1-four-planes" && entry.originalPointRequest.sha256 == family.pointRequestSHA256)
            let point = try decoder.decode(PointRequest.self, from: entry.originalPointRequest.read())
            _ = try Pin(path: point.model.libraryPath, sha256: point.model.expectedSHA256, bytes: point.model.expectedByteCount).read()
            let library = try await EvaluationArtifact(point.model.libraryPath).load()
            defer { do { try library.close() } catch { Issue.record("Actual library cleanup: \(error)") } }
            try #require(library.reference.digest.hexadecimalValue == point.model.expectedSHA256)
            let preparation = try point.model.preparation()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: point.model.descriptorIndex, options: preparation)
            defer { model.close() }
            let factory = OSDINativePrecisionSessionPreparer()
            let program = try factory.snapshot(library: library, descriptorIndex: point.model.descriptorIndex, model: model, preparation: preparation,
                semanticProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: point.semanticProfileSHA256), options: policy(proof, capture: captured))
            try #require(program.programBytes == Array(programData) && program.setupBytes == Array(setupData))
            try #require(program.modelReference == library.reference && program.topology == model.topology)
            try #require(program.topology.nodes.count == entry.rawNodeCount && program.topology.activeUnknownCount == entry.activeUnknownCount)
            let worker = try EvaluationArtifact(Self.environment("OSDI_PRECISION_WORKER"))
            let numeric = try ContentDigest(algorithm: .sha256, hexadecimalValue: Self.environment("OSDI_PRECISION_NUMERIC_PROFILE_SHA256"))
            var session = try await factory.prepareConstitutiveIdentities(program: program, worker: worker.intent, source: worker.source,
                numericProfile: numeric, options: policy(proof, capture: captured), coverLimits: coverLimits(proof), limits: identityLimits(proof),
            coordinateQuotient: .identity(topology: (program).topology))
            try await worker.source.close().wait()
            let first = try evaluate(&session, capture: captured) { view in
                compare(view, expected: reference.result, program: program, capture: captured)
                return (view.cover.completedWorkUnits, view.cover.completedCopiedBytes)
            }
            let second = try evaluate(&session, capture: captured) { view in
                compare(view, expected: reference.result, program: program, capture: captured)
                #expect(view.cover.requestSequence == 2 && view.cover.completedWorkUnits > first.0 && view.cover.completedCopiedBytes > first.1)
                return view.cover.completedWorkUnits
            }
            try session.close(); try session.close()
            var called = false
            do { try evaluate(&session, capture: captured) { _ in called = true }; Issue.record("Closed actual session published") }
            catch OSDIPrecisionConstitutiveError.runtime(.closed) {}
            #expect(!called)
            let cappedWorker = try EvaluationArtifact(Self.environment("OSDI_PRECISION_WORKER"))
            var capped = try await factory.prepareConstitutiveIdentities(program: program, worker: cappedWorker.intent, source: cappedWorker.source,
                numericProfile: numeric, options: policy(proof, capture: captured, work: second-1), coverLimits: coverLimits(proof), limits: identityLimits(proof),
            coordinateQuotient: .identity(topology: (program).topology))
            try await cappedWorker.source.close().wait(); try evaluate(&capped, capture: captured) { _ in }
            do { try evaluate(&capped, capture: captured) { _ in called = true }; Issue.record("Actual cumulative work limit reset") }
            catch OSDIPrecisionConstitutiveError.runtime(.precision(let issue)) { #expect(issue.workerStatus == 7) }
            #expect(!called); try capped.close()
            model.close(); try library.close()
        }
    }
}
