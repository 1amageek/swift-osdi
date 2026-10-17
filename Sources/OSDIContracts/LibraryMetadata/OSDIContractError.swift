public enum OSDIContractError: Error, Sendable, Equatable {
    case invalidEvaluationContext
    case unsupportedEvaluationContext
    case invalidLimits
    case invalidParameterValue
    case invalidPreparation
    case invalidEvaluationLimits
    case invalidTopology
    case invalidDescriptor
}
