import Foundation
import OSDIContracts

struct OSDIEvaluateRequest: Codable {
    struct Limits: Codable {
        let maximumDescriptors: UInt32
        let maximumMetadataBytes: UInt64
        let maximumStorageBytes: UInt64
    }
    struct ReadBudget: Codable {
        let maximumPageByteCount: UInt64
        let maximumTotalByteCount: UInt64
        let maximumPageCount: UInt64
        let maximumWorkUnitCount: UInt64
        let maximumDurationNanoseconds: UInt64
    }
    struct EvaluationLimits: Codable {
        let maximumWorkspaceBytes: UInt64
        let maximumDiagnosticCount: UInt32
        let maximumDiagnosticBytes: UInt64
        let maximumStringBytes: UInt64
    }
    struct Value: Codable {
        let kind: String
        let real: Double?
        let integer: Int32?
        let string: String?

        init(_ value: OSDIParameterValue) {
            switch value {
            case .real(let value): kind = "real"; real = value; integer = nil; string = nil
            case .integer(let value): kind = "integer"; real = nil; integer = value; string = nil
            case .string(let value): kind = "string"; real = nil; integer = nil; string = value
            }
        }

        func runtimeValue() throws -> OSDIParameterValue {
            switch kind {
            case "real" where integer == nil && string == nil:
                guard let real, real.isFinite else { throw OSDIEvaluateFailure.invalidParameter("Expected a finite real scalar") }
                return .real(real)
            case "integer" where real == nil && string == nil:
                guard let integer else { throw OSDIEvaluateFailure.invalidParameter("Expected an Int32 scalar") }
                return .integer(integer)
            case "string" where real == nil && integer == nil:
                guard let string, !string.utf8.contains(0) else { throw OSDIEvaluateFailure.invalidParameter("Expected a NUL-free string scalar") }
                return .string(string)
            case "array": throw OSDIEvaluateFailure.unsupportedCapability("Array parameter assignments")
            default: throw OSDIEvaluateFailure.invalidParameter("Unknown or conflicting scalar type")
            }
        }
    }
    struct Parameter: Codable {
        let name: String
        let scope: String
        let value: Value

        func assignment() throws -> OSDIParameterAssignment {
            let resolvedScope: OSDIParameterScope
            switch scope {
            case "model": resolvedScope = .model
            case "instance": resolvedScope = .instance
            default: throw OSDIEvaluateFailure.invalidParameter("Unknown parameter scope")
            }
            return try OSDIParameterAssignment(name: name, scope: resolvedScope, value: value.runtimeValue())
        }
    }
    struct SimulationParameter: Codable {
        let name: String
        let value: Value

        func assignment() throws -> OSDISimulationParameter {
            let value = try value.runtimeValue()
            guard value.type != .integer else {
                throw OSDIEvaluateFailure.invalidParameter("Simulation parameters require real or string values")
            }
            return try OSDISimulationParameter(name: name, value: value)
        }
    }

    let schemaVersion: Int
    let operation: String
    let libraryPath: String
    let expectedSHA256: String
    let expectedByteCount: UInt64
    let limits: Limits
    let readBudget: ReadBudget
    let descriptorIndex: Int
    let profile: String
    let parameters: [Parameter]
    let simulationParameters: [SimulationParameter]
    let temperatureKelvin: Double
    let connectedTerminalCount: Int
    let evaluationLimits: EvaluationLimits
    let localUnknowns: [Double]?
    let previousState: [Double]?

    func preparationOptions() throws -> OSDIPreparationOptions {
        guard schemaVersion == 1 else { throw OSDIEvaluateFailure.invalidRequest("Unsupported schema version") }
        guard operation == "prepare" || operation == "evaluate" else {
            throw OSDIEvaluateFailure.invalidRequest("Expected prepare or evaluate operation")
        }
        guard profile == "dc-static-point" else { throw OSDIEvaluateFailure.unsupportedCapability("Unsupported evaluation profile") }
        guard temperatureKelvin.isFinite, temperatureKelvin > 0 else { throw OSDIEvaluateFailure.invalidTemperature }
        guard descriptorIndex >= 0, connectedTerminalCount >= 0 else {
            throw OSDIEvaluateFailure.invalidRequest("Negative descriptor index or terminal count")
        }
        let l = evaluationLimits
        return try OSDIPreparationOptions(parameters: parameters.map { try $0.assignment() },
            simulationParameters: simulationParameters.map { try $0.assignment() },
            temperatureKelvin: temperatureKelvin, connectedTerminalCount: connectedTerminalCount,
            limits: OSDIEvaluationLimits(maximumWorkspaceBytes: l.maximumWorkspaceBytes,
                maximumDiagnosticCount: l.maximumDiagnosticCount,
                maximumDiagnosticBytes: l.maximumDiagnosticBytes, maximumStringBytes: l.maximumStringBytes))
    }
}
