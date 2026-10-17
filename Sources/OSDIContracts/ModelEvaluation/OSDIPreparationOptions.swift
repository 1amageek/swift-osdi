/// Immutable point-model configuration. Changing it requires preparing a new session.
public struct OSDIPreparationOptions: Sendable, Equatable {
    public let parameters: [OSDIParameterAssignment]
    public let simulationParameters: [OSDISimulationParameter]
    public let temperatureKelvin: Double
    public let connectedTerminalCount: Int
    public let limits: OSDIEvaluationLimits

    public init(parameters: [OSDIParameterAssignment], simulationParameters: [OSDISimulationParameter],
                temperatureKelvin: Double, connectedTerminalCount: Int,
                limits: OSDIEvaluationLimits) throws(OSDIContractError) {
        guard temperatureKelvin.isFinite, temperatureKelvin > 0,
              connectedTerminalCount >= 0 else { throw .invalidPreparation }
        var names = Set<String>()
        for parameter in simulationParameters {
            guard names.insert(parameter.name).inserted else { throw .invalidParameterValue }
        }
        self.parameters = parameters
        self.simulationParameters = simulationParameters
        self.temperatureKelvin = temperatureKelvin
        self.connectedTerminalCount = connectedTerminalCount
        self.limits = limits
    }
}
