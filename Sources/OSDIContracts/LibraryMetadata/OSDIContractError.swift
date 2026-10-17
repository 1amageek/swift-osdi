public enum OSDIContractError: Error, Sendable, Equatable {
    case invalidLimits
    case invalidParameterValue
    case invalidPreparation
    case invalidEvaluationLimits
    case invalidTopology
    case invalidDescriptor
}
