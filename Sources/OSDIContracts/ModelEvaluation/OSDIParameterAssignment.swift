/// An explicit assignment; model scope also permits defaults for instance parameters.
public struct OSDIParameterAssignment: Sendable, Equatable {
    public let name: String
    public let scope: OSDIParameterScope
    public let value: OSDIParameterValue

    public init(name: String, scope: OSDIParameterScope, value: OSDIParameterValue) throws(OSDIContractError) {
        guard !name.isEmpty, !name.utf8.contains(0), value.isValid else { throw .invalidParameterValue }
        self.name = name
        self.scope = scope
        self.value = value
    }
}
