/// Effective scalar value and explicit-given fact, read after generated setup.
public struct OSDIResolvedParameter: Sendable, Equatable {
    public let parameter: OSDIParameterInfo
    public let scope: OSDIParameterScope
    public let value: OSDIParameterValue
    public let isGiven: Bool

    public init(parameter: OSDIParameterInfo, scope: OSDIParameterScope,
                value: OSDIParameterValue, isGiven: Bool) throws(OSDIContractError) {
        guard parameter.arrayLength == 0, parameter.kind != .operatingPoint,
              parameter.type == value.type, value.isValid,
              scope != .instance || parameter.kind == .instance else { throw .invalidParameterValue }
        self.parameter = parameter
        self.scope = scope
        self.value = value
        self.isGiven = isGiven
    }
}
