/// Copied foreign metadata. Names contain the canonical name followed by aliases.
public struct OSDIParameterInfo: Sendable, Equatable {
    public let index: Int
    public let names: [String]
    public let description: String
    public let units: String
    public let kind: OSDIParameterKind
    public let type: OSDIParameterType
    public let arrayLength: Int

    public init(index: Int, names: [String], kind: OSDIParameterKind,
                type: OSDIParameterType, arrayLength: Int, description: String = "", units: String = "") throws(OSDIContractError) {
        guard index >= 0, !names.isEmpty, names.allSatisfy({ !$0.isEmpty && !$0.utf8.contains(0) }),
              arrayLength >= 0 else { throw .invalidParameterValue }
        self.index = index
        self.names = names
        self.description = description
        self.units = units
        self.kind = kind
        self.type = type
        self.arrayLength = arrayLength
    }
}
