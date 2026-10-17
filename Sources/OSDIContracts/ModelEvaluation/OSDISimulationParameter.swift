/// A model-visible numeric or string simulation parameter; unspecified values are not invented.
public struct OSDISimulationParameter: Sendable, Equatable {
    public let name: String
    public let value: OSDIParameterValue

    public init(name: String, value: OSDIParameterValue) throws(OSDIContractError) {
        guard !name.isEmpty, !name.utf8.contains(0), value.isValid,
              value.type != .integer else { throw .invalidParameterValue }
        self.name = name
        self.value = value
    }
}
