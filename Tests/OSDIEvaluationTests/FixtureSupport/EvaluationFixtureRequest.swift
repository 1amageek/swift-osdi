import OSDIContracts

struct EvaluationFixtureRequest: Decodable, Sendable {
    struct Parameter: Decodable, Sendable {
        let name: String
        let scope: String
        let value: EvaluationParameterValue

        func assignment() throws -> OSDIParameterAssignment {
            let selected: OSDIParameterScope
            switch scope {
            case "model": selected = .model
            case "instance": selected = .instance
            default: throw EvaluationFixtureError.invalidReference("parameter scope \(scope)")
            }
            return try OSDIParameterAssignment(name: name, scope: selected, value: value.value())
        }
    }
    let id: String
    let library: String
    let descriptorIndex: Int
    let temperatureKelvin: Double
    let connectedTerminalCount: Int
    let parameters: [Parameter]
    let rawPotentials: [String: Double]
    let readParameters: [String]

    func options(parameters replacement: [OSDIParameterAssignment]? = nil,
                 limits replacementLimits: OSDIEvaluationLimits? = nil) throws -> OSDIPreparationOptions {
        let limits = try replacementLimits ?? Self.limits()
        let assignments = try replacement ?? parameters.map { try $0.assignment() }
        return try OSDIPreparationOptions(parameters: assignments,
            simulationParameters: [OSDISimulationParameter(name: "gmin", value: .real(0))],
            temperatureKelvin: temperatureKelvin, connectedTerminalCount: connectedTerminalCount,
            limits: limits)
    }

    static func limits() throws -> OSDIEvaluationLimits {
        try OSDIEvaluationLimits(maximumWorkspaceBytes: 16_777_216,
            maximumDiagnosticCount: 128, maximumDiagnosticBytes: 65_536,
            maximumStringBytes: 1_048_576)
    }
}
