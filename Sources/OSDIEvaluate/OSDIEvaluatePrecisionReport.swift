import CircuiteFoundation
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts
import OSDIRuntime

/// Inspection evidence only. Decoding never reconstructs executable precision state.
struct OSDIEvaluatePrecisionReport: Codable {
    typealias Values = OSDIEvaluatePrecisionRequest.Values
    struct Program: Codable {
        let modelReference: ArtifactReference
        let descriptorIndex: UInt32
        let topology: OSDIEvaluateReport.Topology
        let programBytes: [UInt8]
        let setupBytes: [UInt8]
        let programSHA256: String
        let setupSHA256: String
        let semanticProfileSHA256: String
        init(_ value: OSDIPrecisionProgram) {
            modelReference = value.modelReference; descriptorIndex = value.descriptorIndex
            topology = .init(value.topology); programBytes = value.programBytes; setupBytes = value.setupBytes
            programSHA256 = value.programDigest.hexadecimalValue; setupSHA256 = value.setupDigest.hexadecimalValue
            semanticProfileSHA256 = value.semanticProfile.hexadecimalValue
        }
    }
    struct Evaluation: Codable {
        let fHigh: Values
        let fLow: Values
        let fRadius: Values
        let deltaQHigh: Values
        let deltaQLow: Values
        let deltaQRadius: Values
        let g: Values
        let c: Values
        let maximumTimestep: Values?
        let usedPrecisionBits: UInt32
        let completedWorkUnits: UInt64
        let requestSequence: UInt64
        init(_ view: borrowing OSDIPrecisionEvaluationView) {
            fHigh = .init(view.fHigh); fLow = .init(view.fLow); fRadius = .init(view.fRadius)
            deltaQHigh = .init(view.deltaQHigh); deltaQLow = .init(view.deltaQLow); deltaQRadius = .init(view.deltaQRadius)
            g = .init(view.g); c = .init(view.c); maximumTimestep = view.maximumTimestep.map { Values([$0]) }
            usedPrecisionBits = view.usedPrecisionBits; completedWorkUnits = view.completedWorkUnits; requestSequence = view.requestSequence
        }
    }
    struct Failure: Codable {
        struct Precision: Codable {
            let code: Int32
            let workerStatus: UInt32
            let instruction: UInt32?
            let exitCode: Int32?
            let signal: Int32?
            let systemErrno: Int32
            let requestSequence: UInt64
            init(_ issue: OSDIPrecisionIssue) {
                code = issue.code.rawValue; workerStatus = issue.workerStatus; instruction = issue.instruction
                exitCode = issue.exitCode; signal = issue.signal; systemErrno = issue.systemErrno; requestSequence = issue.requestSequence
            }
        }
        let reason: String
        let detail: String
        let precision: Precision?
        let causes: [Failure]
        init(_ error: any Error) {
            detail = String(describing: error)
            var reason = "runtime_failure", precision: Precision?, causes: [Failure] = []
            if let error = error as? OSDIError {
                switch error {
                case .precision(let issue):
                    reason = "precision_" + String(describing: issue.code); precision = .init(issue)
                case .cleanup(let primary, let secondary), .pendingCleanup(let primary, let secondary, _):
                    reason = "cleanup_failure"; causes = [Self(primary), Self(secondary)]
                case .precisionCleanup(let cause, _), .cleanupRequired(let cause, _):
                    reason = "cleanup_failure"; causes = [Self(cause)]
                case .identityMismatch: reason = "identity_mismatch"
                case .resourceLimit, .allocationFailed: reason = "resource_limit"
                case .cancelled: reason = "cancelled"
                case .contract: reason = "invalid_request"
                default: break
                }
            } else if error is DecodingError || error is OSDIEvaluateFailure { reason = "invalid_request" }
            self.reason = reason; self.precision = precision; self.causes = causes
        }
    }
    let schemaVersion: Int
    let stage: String
    let succeeded: Bool
    let requestBinding: OSDIEvaluateReport.Binding?
    let input: OSDIEvaluatePrecisionRequest?
    let workerReference: ArtifactReference?
    let numericProfileSHA256: String?
    let program: Program?
    let prepared: OSDIEvaluateReport.Prepared?
    let evaluation: Evaluation?
    let failure: Failure?

    init(binding: OSDIEvaluateReport.Binding?, input: OSDIEvaluatePrecisionRequest?, program: Program? = nil,
         prepared: OSDIEvaluateReport.Prepared? = nil, evaluation: Evaluation? = nil, failure: Failure? = nil) {
        schemaVersion = 1; stage = input?.operation == "snapshot" ? "osdi-precision-snapshot" : "osdi-precision-evaluation"; succeeded = failure == nil
        requestBinding = binding; self.input = input; workerReference = program == nil ? nil : input?.worker.reference
        numericProfileSHA256 = program == nil ? nil : input?.numericProfileSHA256
        self.program = program; self.prepared = prepared; self.evaluation = evaluation; self.failure = failure
    }
    func validate() throws {
        guard schemaVersion == 1, ["osdi-precision-evaluation", "osdi-precision-snapshot"].contains(stage) else { throw OSDIEvaluateFailure.reportRoundTrip }
        if !succeeded {
            guard failure != nil, program == nil, prepared == nil, evaluation == nil else { throw OSDIEvaluateFailure.reportRoundTrip }; return
        }
        guard failure == nil, let input, requestBinding != nil, let program, let prepared,
              workerReference == input.worker.reference, numericProfileSHA256 == input.numericProfileSHA256,
              program.semanticProfileSHA256 == input.semanticProfileSHA256,
              program.descriptorIndex == input.model.descriptorIndex,
              program.modelReference.digest.hexadecimalValue == input.model.expectedSHA256,
              program.modelReference.byteCount == input.model.expectedByteCount else { throw OSDIEvaluateFailure.reportRoundTrip }
        try input.validate()
        guard try OSDIEvaluatePrecisionCommand.binding(program.programBytes).sha256 == program.programSHA256,
              try OSDIEvaluatePrecisionCommand.binding(program.setupBytes).sha256 == program.setupSHA256 else { throw OSDIEvaluateFailure.reportRoundTrip }
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        guard try encoder.encode(program.topology) == encoder.encode(prepared.topology),
              UInt64(program.programBytes.count) <= input.options.maximumProgramBytes,
              UInt64(program.setupBytes.count) <= input.options.maximumMappingBytes else { throw OSDIEvaluateFailure.reportRoundTrip }
        let count = program.topology.nodes.count
        let (matrix, overflow) = count.multipliedReportingOverflow(by: count)
        guard !overflow, count > 0, prepared.topology.activeUnknownCount == program.topology.activeUnknownCount,
              prepared.topology.nodes.count == count else { throw OSDIEvaluateFailure.reportRoundTrip }
        if input.operation == "snapshot" {
            guard stage == "osdi-precision-snapshot", evaluation == nil else { throw OSDIEvaluateFailure.reportRoundTrip }; return
        }
        guard stage == "osdi-precision-evaluation", let evaluation, let candidateHigh = input.candidateHigh,
              program.topology.activeUnknownCount == candidateHigh.values.count,
              evaluation.usedPrecisionBits >= input.precisionBits,
              evaluation.usedPrecisionBits <= input.options.maximumPrecisionBits,
              evaluation.completedWorkUnits <= input.options.maximumWorkUnits,
              evaluation.requestSequence == 1 else { throw OSDIEvaluateFailure.reportRoundTrip }
        for vector in [evaluation.fHigh, evaluation.fLow, evaluation.fRadius, evaluation.deltaQHigh, evaluation.deltaQLow, evaluation.deltaQRadius] {
            try vector.validate(); guard vector.values.count == count else { throw OSDIEvaluateFailure.reportRoundTrip }
        }
        for vector in [evaluation.g, evaluation.c] {
            try vector.validate(); guard vector.values.count == matrix else { throw OSDIEvaluateFailure.reportRoundTrip }
        }
        guard evaluation.fRadius.values.allSatisfy({ $0 >= 0 }), evaluation.deltaQRadius.values.allSatisfy({ $0 >= 0 }) else { throw OSDIEvaluateFailure.reportRoundTrip }
        if let bound = evaluation.maximumTimestep {
            try bound.validate(); guard bound.values.count == 1, bound.values[0] > 0 else { throw OSDIEvaluateFailure.reportRoundTrip }
        }
    }
}
