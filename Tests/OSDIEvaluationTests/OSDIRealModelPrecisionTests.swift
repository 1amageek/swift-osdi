import CircuiteFoundation
import CircuiteFoundationCrypto
import Foundation
import OSDIContracts
import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIRealModelPrecisionTests {
    private static let families = ["bulk", "cmg", "img", "soi"]

    private struct FileFact: Decodable {
        let path: String
        let bytes: Int
        let sha256: String

        func read() throws -> Data {
            let data = try Data(contentsOf: URL(fileURLWithPath: path))
            guard data.count == bytes, try OSDIRealModelPrecisionTests.digest(data) == sha256 else {
                throw EvaluationFixtureError.invalidReference("Exact evidence file: \(path)")
            }
            return data
        }
    }

    private struct Context: Decodable {
        let mode: UInt32
        let high: Double
        let low: Double
    }

    private struct Baseline: Decodable {
        struct Family: Decodable {
            let outputs: [[Double]]
            let vectors: [[Double]]
            let contexts: [Context]
            let precisionBits: UInt32
            let programSHA256: String
            let setupSHA256: String
            let semanticProfileSHA256: String
            let status: Int
            let exit: Int
        }
        let families: [String: Family]
        let succeeded: Bool
        let numericProfileSHA256: String
        let workerSHA256: String
    }

    private struct IndependentComparison: Decodable {
        struct Family: Decodable {
            let family: String
            let passed: Bool
            let channels: Int
            let receipt: FileFact
        }
        let schema: String
        let passed: Bool
        let workerReport: FileFact
        let independentBinary: FileFact
        let families: [Family]
    }

    private struct IndependentReplay: Decodable {
        struct Inputs: Decodable {
            let binary: FileFact
            let program: FileFact
            let setup: FileFact
            let points: FileFact
        }
        struct Execution: Decodable {
            let exitCode: Int
            let timedOut: Bool
            let stdout: FileFact
            let stderr: FileFact
        }
        let schema: String
        let inputs: Inputs
        let precisionBits: Int
        let vectors: [[Double]]
        let contexts: [Context]
        let execution: Execution
        let status: String
    }

    private struct PublicComparison: Decodable {
        struct Family: Decodable {
            let family: String
            let passed: Bool
            let channels: Int
            let report: FileFact
        }
        let schema: String
        let passed: Bool
        let workerReport: FileFact
        let independentComparison: FileFact
        let families: [Family]
    }

    private struct Request: Decodable, Sendable {
        struct Model: Decodable, Sendable {
            struct SimulationParameter: Decodable, Sendable {
                let name: String
                let value: EvaluationParameterValue
            }
            struct Limits: Decodable, Sendable {
                let maximumWorkspaceBytes: UInt64
                let maximumDiagnosticCount: UInt32
                let maximumDiagnosticBytes: UInt64
                let maximumStringBytes: UInt64
            }
            let libraryPath: String
            let expectedSHA256: String
            let expectedByteCount: Int
            let descriptorIndex: Int
            let parameters: [EvaluationFixtureRequest.Parameter]
            let simulationParameters: [SimulationParameter]
            let temperatureKelvin: Double
            let connectedTerminalCount: Int
            let evaluationLimits: Limits

            func preparation() throws -> OSDIPreparationOptions {
                try OSDIPreparationOptions(parameters: parameters.map { try $0.assignment() },
                    simulationParameters: simulationParameters.map {
                        try OSDISimulationParameter(name: $0.name, value: $0.value.value())
                    }, temperatureKelvin: temperatureKelvin, connectedTerminalCount: connectedTerminalCount,
                    limits: OSDIEvaluationLimits(maximumWorkspaceBytes: evaluationLimits.maximumWorkspaceBytes,
                        maximumDiagnosticCount: evaluationLimits.maximumDiagnosticCount,
                        maximumDiagnosticBytes: evaluationLimits.maximumDiagnosticBytes,
                        maximumStringBytes: evaluationLimits.maximumStringBytes))
            }
        }
        struct Values: Decodable, Sendable {
            let values: [Double]
            let bitPatterns: [UInt64]
            func validate() throws {
                guard values.allSatisfy(\.isFinite), values.map(\.bitPattern) == bitPatterns else {
                    throw EvaluationFixtureError.invalidReference("Paired coordinate bits")
                }
            }
        }
        struct Time: Decodable, Sendable {
            let mode: String
            let timeHigh: Double
            let timeLow: Double
            let timeHighBits: UInt64
            let timeLowBits: UInt64
            func context() throws -> OSDIPrecisionContext {
                guard mode == "transient", timeHigh.bitPattern == timeHighBits,
                      timeLow.bitPattern == timeLowBits else {
                    throw EvaluationFixtureError.invalidReference("Transient context bits")
                }
                return try OSDIPrecisionContext(mode: .transient, timeHigh: timeHigh, timeLow: timeLow)
            }
        }
        struct Options: Decodable, Sendable {
            let maximumProgramBytes: UInt64
            let maximumMappingBytes: UInt64
            let arenaBytes: UInt64
            let maximumWorkUnits: UInt64
            let minimumPrecisionBits: UInt32
            let maximumPrecisionBits: UInt32
            let startupTimeoutNanoseconds: UInt64
            let evaluationTimeoutNanoseconds: UInt64
            let cleanupTimeoutNanoseconds: UInt64
            let pollNanoseconds: UInt64
            func policy(maximumWork: UInt64? = nil) throws -> OSDIPrecisionOptions {
                try OSDIPrecisionOptions(maximumProgramBytes: maximumProgramBytes,
                    maximumMappingBytes: maximumMappingBytes, arenaBytes: arenaBytes,
                    maximumWorkUnits: maximumWork ?? maximumWorkUnits,
                    minimumPrecisionBits: minimumPrecisionBits, maximumPrecisionBits: maximumPrecisionBits,
                    startupTimeoutNanoseconds: startupTimeoutNanoseconds,
                    evaluationTimeoutNanoseconds: evaluationTimeoutNanoseconds,
                    cleanupTimeoutNanoseconds: cleanupTimeoutNanoseconds, pollNanoseconds: pollNanoseconds)
            }
        }
        let model: Model
        let semanticProfileSHA256: String
        let numericProfileSHA256: String
        let precisionBits: UInt32
        let options: Options
        let candidateHigh: Values
        let candidateLow: Values
        let referenceHigh: Values
        let referenceLow: Values
        let candidateContext: Time
        let referenceContext: Time

        var vectors: [[Double]] { [candidateHigh.values, candidateLow.values, referenceHigh.values, referenceLow.values] }
    }

    private struct PublicReport: Decodable {
        struct Binding: Decodable { let byteCount: Int; let sha256: String }
        let requestBinding: Binding
    }

    private struct SOIQualification: Decodable {
        struct Case: Decodable { let id: String; let request: FileFact; let report: FileFact }
        let schema: String
        let passed: Bool
        let source: FileFact
        let model: FileFact
        let compiler: FileFact
        let compileReceipt: FileFact
        let acquisition: FileFact
        let semanticProfile: FileFact
        let selectedWorker: FileFact
        let selectedNumericProfile: FileFact
        let modelVerification: FileFact
        let independentAdmission: FileFact
        let independentComparison: FileFact
        let coldResourceRefusals: [Case]
        let cases: [Case]
    }

    private struct Compilation: Decodable {
        let sourceSHA256: String
        let imageSHA256: String
        let compilerSHA256: String
        let semanticProfileSHA256: String
        let exitCode: Int
    }

    private struct Acquisition: Decodable { let url: String; let files: [FileFact] }

    private struct NumericProfile: Decodable {
        struct Worker: Decodable { let sha256: String }
        let worker: Worker
    }

    private struct OracleAdmission: Decodable {
        let status: String
        let originalBuild: FileFact
        let originalSources: [FileFact]
        let sources: [FileFact]
        let binary: FileFact
        let diff: FileFact
    }

    private struct ColdResourceRefusal: Decodable {
        struct Failure: Decodable {
            struct Precision: Decodable { let code: Int; let exitCode: Int }
            let reason: String
            let precision: Precision
        }
        let succeeded: Bool
        let failure: Failure
        let requestBinding: PublicReport.Binding
    }

    private static func environment(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else {
            throw EvaluationFixtureError.requiredEnvironment(key)
        }
        return value
    }

    private static func digest(_ data: Data) throws -> String {
        try SHA256ContentDigester().digest(using: .sha256, limits: .singleBufferMaximum) { lease throws(ContentDigestError) in
            try lease.update(Array(data))
        }.digest.hexadecimalValue
    }

    private static func coldSnapshotIdentity(_ data: Data, cold: Bool) throws -> NSDictionary {
        guard var value = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              var options = value["options"] as? [String: Any],
              let arena = options["arenaBytes"] as? NSNumber,
              value["operation"] as? String == (cold ? "snapshot" : "evaluate"),
              arena.uint64Value == (cold ? 67_108_864 : 268_435_456) else {
            throw EvaluationFixtureError.invalidReference("Cold refusal operating profile")
        }
        let fields = ["candidateHigh", "candidateLow", "referenceHigh", "referenceLow", "candidateContext", "referenceContext"]
        guard !cold || fields.allSatisfy({ value[$0] == nil }) else {
            throw EvaluationFixtureError.invalidReference("Snapshot with evaluation inputs")
        }
        value["operation"] = "snapshot"
        options["arenaBytes"] = 67_108_864
        value["options"] = options
        value.removeValue(forKey: "stagingParentDirectory")
        for field in fields { value.removeValue(forKey: field) }
        return NSDictionary(dictionary: value)
    }

    private static func validateColdRefusals(_ qualification: SOIQualification, decoder: JSONDecoder) throws {
        let ids = Set(["soi101-n-pd", "soi101-n-dd", "soi101-p-pd", "soi101-p-dd"])
        guard qualification.coldResourceRefusals.count == 4,
              Set(qualification.coldResourceRefusals.map(\.id)) == ids else {
            throw EvaluationFixtureError.invalidReference("Cold refusal case coverage")
        }
        for pair in qualification.coldResourceRefusals {
            guard let success = qualification.cases.first(where: { $0.id == pair.id }) else {
                throw EvaluationFixtureError.invalidReference("Cold refusal success case")
            }
            let requestData = try pair.request.read(), reportData = try pair.report.read()
            let report = try decoder.decode(ColdResourceRefusal.self, from: reportData)
            guard let record = try JSONSerialization.jsonObject(with: reportData) as? [String: Any],
                  record["evaluation"] == nil, record["program"] == nil,
                  !report.succeeded, report.failure.reason == "precision_resourceLimit",
                  report.failure.precision.code == 2, report.failure.precision.exitCode == 80,
                  report.requestBinding.byteCount == requestData.count,
                  report.requestBinding.sha256 == pair.request.sha256,
                  try coldSnapshotIdentity(requestData, cold: true).isEqual(coldSnapshotIdentity(success.request.read(), cold: false)) else {
                throw EvaluationFixtureError.invalidReference("Cold refusal request/report identity")
            }
        }
    }

    private func bits(_ values: borrowing Span<Double>) -> [UInt64] {
        var result: [UInt64] = []; result.reserveCapacity(values.count)
        for index in 0..<values.count { result.append(values[index].bitPattern) }
        return result
    }

    private func evaluate<Result>(session: inout OSDIPrecisionSession, request: Request,
        _ body: (OSDIPrecisionEvaluationView) -> Result) throws -> Result {
        try OSDINativePrecisionEvaluator().evaluate(session: &session,
            candidateHigh: request.candidateHigh.values.span, candidateLow: request.candidateLow.values.span,
            referenceHigh: request.referenceHigh.values.span, referenceLow: request.referenceLow.values.span,
            candidateContext: request.candidateContext.context(), referenceContext: request.referenceContext.context(),
            precisionBits: request.precisionBits, body)
    }

    private func ordinary(model: inout OSDIPreparedModel, request: Request) throws -> [[UInt64]] {
        let previous: [Double] = []
        return try OSDINativeModelEvaluator().evaluate(model: &model,
            unknowns: request.candidateHigh.values.span, previousState: previous.span) {
                [bits($0.f), bits($0.q), bits($0.g), bits($0.c)]
            }
    }

    @Test func fourRealFamiliesRetainIndependentlyQualifiedPairedPointAndOwnerLifetimes() async throws {
        let decoder = JSONDecoder()
        let referenceData = try Data(contentsOf: URL(fileURLWithPath: Self.environment("OSDI_REAL_PRECISION_REFERENCE")))
        let baseline = try decoder.decode(Baseline.self, from: referenceData)
        let publicData = try Data(contentsOf: URL(fileURLWithPath: Self.environment("OSDI_REAL_PRECISION_INDEPENDENT_RECEIPT")))
        let receipt = try decoder.decode(PublicComparison.self, from: publicData)
        let independent = try decoder.decode(IndependentComparison.self, from: receipt.independentComparison.read())
        try #require(baseline.succeeded && receipt.passed && independent.passed)
        try #require(receipt.schema == "corespice.independent-precision-public-path.v1")
        try #require(independent.schema == "corespice.independent-precision-containment.v1")
        try #require(try receipt.workerReport.read() == referenceData)
        try #require(try independent.workerReport.read() == referenceData)
        try #require(Set(receipt.families.map(\.family)) == Set(Self.families) && receipt.families.count == 4)
        try #require(Set(independent.families.map(\.family)) == Set(Self.families) && independent.families.count == 4)
        try #require(independent.families.reduce(0) { $0 + $1.channels } == 116)
        _ = try independent.independentBinary.read()
        let requests = try URL(fileURLWithPath: Self.environment("OSDI_REAL_PRECISION_REQUEST_DIRECTORY"))

        for family in Self.families {
            let expected = try #require(baseline.families[family])
            let publicFamily = try #require(receipt.families.first { $0.family == family })
            let independentFamily = try #require(independent.families.first { $0.family == family })
            let replay = try decoder.decode(IndependentReplay.self, from: independentFamily.receipt.read())
            try #require(publicFamily.passed && independentFamily.passed && replay.status == "passed")
            try #require(replay.schema == "corespice.independent-precision-replay.v1" && replay.precisionBits == 512)
            try #require(replay.execution.exitCode == 0 && !replay.execution.timedOut)
            try #require(replay.inputs.binary.sha256 == independent.independentBinary.sha256)
            try #require(replay.inputs.program.sha256 == expected.programSHA256 && replay.inputs.setup.sha256 == expected.setupSHA256)
            for fact in [replay.inputs.program, replay.inputs.setup, replay.inputs.points,
                         replay.execution.stdout, replay.execution.stderr] { _ = try fact.read() }
            let requestData = try Data(contentsOf: requests.appendingPathComponent("\(family)-request.json"))
            let request = try decoder.decode(Request.self, from: requestData)
            let publicReport = try decoder.decode(PublicReport.self, from: publicFamily.report.read())
            try #require(publicReport.requestBinding.byteCount == requestData.count)
            try #require(publicReport.requestBinding.sha256 == (try Self.digest(requestData)))
            try #require(request.numericProfileSHA256 == baseline.numericProfileSHA256)
            try #require(request.semanticProfileSHA256 == expected.semanticProfileSHA256)
            try #require(request.precisionBits == expected.precisionBits && expected.status == 0 && expected.exit == 0)
            for vector in [request.candidateHigh, request.candidateLow, request.referenceHigh, request.referenceLow] {
                try vector.validate()
            }
            try #require(request.vectors.map { $0.map(\.bitPattern) } == expected.vectors.map { $0.map(\.bitPattern) })
            try #require(request.vectors.map { $0.map(\.bitPattern) } == replay.vectors.map { $0.map(\.bitPattern) })
            try #require(request.candidateLow.values.contains { $0 != 0 })
            try #require(zip(request.candidateHigh.values, request.candidateLow.values).allSatisfy { $0 + $1 == $0 })
            for (index, context) in [try request.candidateContext.context(), try request.referenceContext.context()].enumerated() {
                for source in [expected.contexts, replay.contexts] {
                    try #require(source.count == 2 && source[index].mode == context.mode.rawValue)
                    try #require(source[index].high.bitPattern == context.timeHigh.bitPattern && source[index].low.bitPattern == context.timeLow.bitPattern)
                }
            }
            let modelFact = FileFact(path: request.model.libraryPath, bytes: request.model.expectedByteCount,
                sha256: request.model.expectedSHA256)
            _ = try modelFact.read()
            let library = try await EvaluationArtifact(request.model.libraryPath).load()
            let preparation = try request.model.preparation()
            var model = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: request.model.descriptorIndex, options: preparation)
            defer { model.close() }
            try #require(model.topology.stateCount == 0)
            let before = try ordinary(model: &model, request: request)
            let factory = OSDINativePrecisionSessionPreparer()
            let program = try factory.snapshot(library: library, descriptorIndex: request.model.descriptorIndex,
                model: model, preparation: preparation,
                semanticProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: request.semanticProfileSHA256),
                options: request.options.policy())
            try #require(program.programDigest.hexadecimalValue == expected.programSHA256)
            try #require(program.setupDigest.hexadecimalValue == expected.setupSHA256)
            try #require(publicFamily.channels == 2 * model.topology.nodes.count && independentFamily.channels == publicFamily.channels)
            let worker = try EvaluationArtifact(Self.environment("OSDI_PRECISION_WORKER"))
            // The selected image must acknowledge its own exact profile, independently of the old baseline.
            let numeric = try ContentDigest(algorithm: .sha256,
                hexadecimalValue: Self.environment("OSDI_PRECISION_NUMERIC_PROFILE_SHA256"))
            var session = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
                numericProfile: numeric, options: request.options.policy())
            // Independent 512-bit receipts qualify F/delta-Q; G/C remain inexact Newton aids.
            let actual = try evaluate(session: &session, request: request) { view in
                #expect(view.usedPrecisionBits == request.precisionBits)
                return [bits(view.fHigh), bits(view.fLow), bits(view.fRadius), bits(view.deltaQHigh),
                        bits(view.deltaQLow), bits(view.deltaQRadius), bits(view.g), bits(view.c)]
            }
            #expect(actual == expected.outputs.map { $0.map(\.bitPattern) }, "\(family): all eight qualified output vectors")
            try session.close(); try session.close()
            var called = false
            do {
                try evaluate(session: &session, request: request) { _ in called = true }
                Issue.record("Closed real model precision session published success: \(family)")
            } catch OSDIError.closed {}
            #expect(!called)
            #expect(try ordinary(model: &model, request: request) == before)
            var refused = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
                numericProfile: numeric, options: request.options.policy(maximumWork: 1))
            do {
                try evaluate(session: &refused, request: request) { _ in called = true }
                Issue.record("Real model work refusal published success: \(family)")
            } catch OSDIError.precision(let issue) { #expect(issue.code == .numericFailure && issue.workerStatus == 7) }
            #expect(!called)
            try refused.close()
            #expect(try ordinary(model: &model, request: request) == before)
            try await worker.source.close().wait()
            if family == Self.families[0] {
                let cancelled = Task {
                    let childWorker = try EvaluationArtifact(Self.environment("OSDI_PRECISION_WORKER"))
                    var child = try await factory.prepare(program: program, worker: childWorker.intent, source: childWorker.source,
                        numericProfile: numeric, options: request.options.policy())
                    try await childWorker.source.close().wait()
                    withUnsafeCurrentTask { $0?.cancel() }
                    var callbackCalled = false
                    do {
                        try evaluate(session: &child, request: request) { _ in callbackCalled = true }
                        Issue.record("Cancelled real model evaluation published success")
                    } catch OSDIError.precision(let issue) { #expect(issue.code == .cancelled) }
                    #expect(!callbackCalled)
                    try child.close()
                }
                try await cancelled.value
                #expect(try ordinary(model: &model, request: request) == before)
            }
            model.close(); try library.close()
        }
    }

    @Test func authenticSOI101NPAndPDDDPointsPreserveIndependentProofAndOrdinaryOwner() async throws {
        let directory = try URL(fileURLWithPath: Self.environment("OSDI_SOI101_PRECISION_DIRECTORY"))
        let decoder = JSONDecoder()
        let qualification = try decoder.decode(SOIQualification.self,
            from: Data(contentsOf: directory.appendingPathComponent("latest-qualification.json")))
        try #require(qualification.passed && qualification.schema == "osdi.soi101.precision-qualified.v2")
        let source = try qualification.source.read()
        try #require(String(decoding: source, as: UTF8.self).contains("BSIM-SOI 101.0.0 released on 09/15/2026"))
        _ = try qualification.model.read(); _ = try qualification.compiler.read()
        _ = try qualification.semanticProfile.read(); _ = try qualification.selectedWorker.read()
        let compilation = try decoder.decode(Compilation.self, from: qualification.compileReceipt.read())
        try #require(compilation.exitCode == 0 && compilation.sourceSHA256 == qualification.source.sha256)
        try #require(compilation.imageSHA256 == qualification.model.sha256 && compilation.compilerSHA256 == qualification.compiler.sha256)
        try #require(compilation.semanticProfileSHA256 == qualification.semanticProfile.sha256)
        let acquisition = try decoder.decode(Acquisition.self, from: qualification.acquisition.read())
        try #require(acquisition.url == "https://si2.org/wp-content/uploads/CMC/models/BSIM-SOI_101.0.0_09152026.tar.gz")
        try #require(acquisition.files.contains { $0.sha256 == qualification.source.sha256 && $0.bytes == source.count })
        let numeric = try decoder.decode(NumericProfile.self, from: qualification.selectedNumericProfile.read())
        try #require(numeric.worker.sha256 == qualification.selectedWorker.sha256)
        let referenceData = try qualification.modelVerification.read()
        let baseline = try decoder.decode(Baseline.self, from: referenceData)
        try #require(baseline.succeeded && baseline.workerSHA256 == qualification.selectedWorker.sha256)
        try #require(baseline.numericProfileSHA256 == qualification.selectedNumericProfile.sha256)
        let independent = try decoder.decode(IndependentComparison.self, from: qualification.independentComparison.read())
        let independentlyComparedData = try independent.workerReport.read()
        try #require(independent.passed && independentlyComparedData == referenceData)
        try #require(independent.families.reduce(0) { $0 + $1.channels } == 112)
        let admission = try decoder.decode(OracleAdmission.self, from: qualification.independentAdmission.read())
        try #require(admission.status == "passed" && admission.binary.sha256 == independent.independentBinary.sha256)
        _ = try admission.binary.read(); _ = try admission.diff.read(); _ = try admission.originalBuild.read()
        let oldSource = try #require(admission.originalSources.first { $0.path.hasSuffix("/reference.c") })
        let newSource = try #require(admission.sources.first { $0.path.hasSuffix("/reference.c") })
        let originalOracle = String(decoding: try oldSource.read(), as: UTF8.self)
        let extendedOracle = String(decoding: try newSource.read(), as: UTF8.self)
        try #require(originalOracle.replacingOccurrences(of: "MAX_REGS=200000", with: "MAX_REGS=250000") == extendedOracle)
        try Self.validateColdRefusals(qualification, decoder: decoder)
        let ids = Set(["soi101-n-pd", "soi101-n-dd", "soi101-p-pd", "soi101-p-dd"])
        try #require(Set(qualification.cases.map(\.id)) == ids && qualification.cases.count == 4)
        try #require(Set(independent.families.map(\.family)) == ids && independent.families.count == 4)
        for point in qualification.cases {
            let expected = try #require(baseline.families[point.id])
            let proof = try #require(independent.families.first { $0.family == point.id })
            let replay = try decoder.decode(IndependentReplay.self, from: proof.receipt.read())
            try #require(proof.passed && proof.channels == 28 && replay.status == "passed" && replay.precisionBits == 512)
            try #require(replay.execution.exitCode == 0 && !replay.execution.timedOut)
            try #require(replay.inputs.binary.sha256 == admission.binary.sha256)
            try #require(replay.inputs.program.sha256 == expected.programSHA256 && replay.inputs.setup.sha256 == expected.setupSHA256)
            for file in [replay.inputs.program, replay.inputs.setup, replay.inputs.points, replay.execution.stdout, replay.execution.stderr] {
                _ = try file.read()
            }
            let requestData = try point.request.read()
            let request = try decoder.decode(Request.self, from: requestData)
            let report = try decoder.decode(PublicReport.self, from: point.report.read())
            try #require(report.requestBinding.sha256 == point.request.sha256 && report.requestBinding.byteCount == requestData.count)
            try #require(request.model.expectedSHA256 == qualification.model.sha256)
            try #require(request.model.expectedByteCount == qualification.model.bytes)
            try #require(request.numericProfileSHA256 == qualification.selectedNumericProfile.sha256)
            try #require(request.semanticProfileSHA256 == qualification.semanticProfile.sha256 && request.precisionBits == 256)
            // The 64 MiB cold refusal is retained separately; this exact source requires explicit 256 MiB admission.
            try #require(request.options.arenaBytes == 268_435_456)
            for vector in [request.candidateHigh, request.candidateLow, request.referenceHigh, request.referenceLow] { try vector.validate() }
            try #require(request.vectors.map { $0.map(\.bitPattern) } == expected.vectors.map { $0.map(\.bitPattern) })
            try #require(request.vectors.map { $0.map(\.bitPattern) } == replay.vectors.map { $0.map(\.bitPattern) })
            for (index, context) in [try request.candidateContext.context(), try request.referenceContext.context()].enumerated() {
                for source in [expected.contexts, replay.contexts] {
                    try #require(source.count == 2 && source[index].mode == context.mode.rawValue)
                    try #require(source[index].high.bitPattern == context.timeHigh.bitPattern && source[index].low.bitPattern == context.timeLow.bitPattern)
                }
            }
            let library = try await EvaluationArtifact(request.model.libraryPath).load()
            try #require(library.reference.digest.hexadecimalValue == qualification.model.sha256)
            let preparation = try request.model.preparation()
            var model = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: request.model.descriptorIndex, options: preparation)
            let before = try ordinary(model: &model, request: request)
            let factory = OSDINativePrecisionSessionPreparer()
            let program = try factory.snapshot(library: library, descriptorIndex: request.model.descriptorIndex,
                model: model, preparation: preparation,
                semanticProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: request.semanticProfileSHA256),
                options: request.options.policy())
            try #require(program.programDigest.hexadecimalValue == expected.programSHA256 && program.setupDigest.hexadecimalValue == expected.setupSHA256)
            let worker = try EvaluationArtifact(qualification.selectedWorker.path)
            try #require(worker.intent.expectedReference.digest.hexadecimalValue == qualification.selectedWorker.sha256)
            var session = try await factory.prepare(program: program, worker: worker.intent, source: worker.source,
                numericProfile: ContentDigest(algorithm: .sha256, hexadecimalValue: qualification.selectedNumericProfile.sha256),
                options: request.options.policy())
            try await worker.source.close().wait()
            let actual = try evaluate(session: &session, request: request) { view in
                #expect(view.usedPrecisionBits == 256)
                return [bits(view.fHigh), bits(view.fLow), bits(view.fRadius), bits(view.deltaQHigh),
                        bits(view.deltaQLow), bits(view.deltaQRadius), bits(view.g), bits(view.c)]
            }
            #expect(actual == expected.outputs.map { $0.map(\.bitPattern) }, "\(point.id): independently qualified F/delta-Q; inexact G/C regression")
            try session.close()
            #expect(try ordinary(model: &model, request: request) == before)
            model.close(); try library.close()
        }
    }
}
