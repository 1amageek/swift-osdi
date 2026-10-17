import CircuiteFoundation
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts

/// Explicit cold precision selection. No field supplies an inferred numeric policy.
struct OSDIEvaluatePrecisionRequest: Codable {
    struct Values: Codable, Equatable {
        let values: [Double]
        let bitPatterns: [UInt64]
        init(_ values: [Double]) {
            self.values = values; bitPatterns = values.map(\.bitPattern)
        }
        init(_ span: borrowing Span<Double>) {
            var values: [Double] = []; values.reserveCapacity(span.count)
            for index in 0..<span.count { values.append(span[index]) }
            self.init(values)
        }
        func validate() throws {
            guard values.count == bitPatterns.count,
                  zip(values, bitPatterns).allSatisfy({ $0.isFinite && $0.bitPattern == $1 }) else {
                throw OSDIEvaluateFailure.invalidUnknowns("Values and finite binary64 bit patterns must agree")
            }
        }
    }
    struct Context: Codable, Equatable {
        let mode: String
        let timeHigh: Double
        let timeLow: Double
        let timeHighBits: UInt64
        let timeLowBits: UInt64
        func checked() throws -> OSDIPrecisionContext {
            guard timeHigh.bitPattern == timeHighBits, timeLow.bitPattern == timeLowBits else {
                throw OSDIEvaluateFailure.invalidRequest("Context bit patterns disagree")
            }
            let selected: OSDIPrecisionContext.Mode
            switch mode {
            case "transient": selected = .transient
            case "transientInitialization": selected = .transientInitialization
            default: throw OSDIEvaluateFailure.unsupportedCapability("Precision context mode is unsupported")
            }
            return try .init(mode: selected, timeHigh: timeHigh, timeLow: timeLow)
        }
    }
    struct Worker: Codable {
        let path: String
        let reference: ArtifactReference
        let readBudget: OSDIEvaluateRequest.ReadBudget
    }
    struct Options: Codable {
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
        func checked() throws -> OSDIPrecisionOptions {
            try .init(maximumProgramBytes: maximumProgramBytes, maximumMappingBytes: maximumMappingBytes,
                arenaBytes: arenaBytes, maximumWorkUnits: maximumWorkUnits, minimumPrecisionBits: minimumPrecisionBits,
                maximumPrecisionBits: maximumPrecisionBits, startupTimeoutNanoseconds: startupTimeoutNanoseconds,
                evaluationTimeoutNanoseconds: evaluationTimeoutNanoseconds, cleanupTimeoutNanoseconds: cleanupTimeoutNanoseconds,
                pollNanoseconds: pollNanoseconds)
        }
    }
    let schemaVersion: Int
    let profile: String
    let operation: String
    let model: OSDIEvaluateRequest
    let worker: Worker
    let stagingParentDirectory: String
    let semanticProfileSHA256: String
    let numericProfileSHA256: String
    let options: Options
    let precisionBits: UInt32
    let candidateHigh: Values?
    let candidateLow: Values?
    let referenceHigh: Values?
    let referenceLow: Values?
    let candidateContext: Context?
    let referenceContext: Context?
    let maximumReportBytes: UInt64

    func validate() throws {
        guard schemaVersion == 1, profile == "stateless-precision-v1", ["snapshot", "evaluate"].contains(operation), model.operation == "prepare",
              model.localUnknowns == nil, model.previousState == nil,
              maximumReportBytes > 0, maximumReportBytes <= UInt64(Int.max),
              [model.libraryPath, worker.path, stagingParentDirectory].allSatisfy({ $0.hasPrefix("/") && !$0.utf8.contains(0) }) else {
            throw OSDIEvaluateFailure.invalidRequest("Invalid explicit precision schema, preparation or paths")
        }
        _ = try model.preparationOptions(); _ = try options.checked()
        if operation == "evaluate" {
            guard let candidateHigh, let candidateLow, let referenceHigh, let referenceLow,
                  let candidateContext, let referenceContext else { throw OSDIEvaluateFailure.invalidRequest("Evaluate requires four explicit vectors and both contexts") }
            _ = try candidateContext.checked(); _ = try referenceContext.checked()
            for vector in [candidateHigh, candidateLow, referenceHigh, referenceLow] { try vector.validate() }
            guard candidateHigh.values.count == candidateLow.values.count,
                  candidateHigh.values.count == referenceHigh.values.count,
                  candidateHigh.values.count == referenceLow.values.count,
                  zip(candidateHigh.values, candidateLow.values).allSatisfy({ $0 + $1 == $0 }),
                  zip(referenceHigh.values, referenceLow.values).allSatisfy({ $0 + $1 == $0 }) else {
                throw OSDIEvaluateFailure.invalidUnknowns("Explicit precision pairs require matching dimensions and nonoverlapping finite values")
            }
        } else {
            guard candidateHigh == nil, candidateLow == nil, referenceHigh == nil, referenceLow == nil,
                  candidateContext == nil, referenceContext == nil else { throw OSDIEvaluateFailure.invalidRequest("Snapshot accepts no evaluation coordinates or contexts") }
        }
        guard precisionBits >= options.minimumPrecisionBits, precisionBits <= options.maximumPrecisionBits else {
            throw OSDIEvaluateFailure.invalidRequest("Selected precision is outside explicit limits")
        }
        _ = try ContentDigest(algorithm: .sha256, hexadecimalValue: semanticProfileSHA256)
        _ = try ContentDigest(algorithm: .sha256, hexadecimalValue: numericProfileSHA256)
        guard worker.reference.digest.algorithm == .sha256 else { throw OSDIEvaluateFailure.invalidRequest("Worker requires exact SHA-256 identity") }
    }
}
