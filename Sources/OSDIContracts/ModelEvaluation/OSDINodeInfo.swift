/// Raw descriptor node and its prepared active coordinate; nil denotes ground.
public struct OSDINodeInfo: Sendable, Equatable {
    public let index: Int
    public let name: String
    public let units: String
    public let residualUnits: String
    public let isFlow: Bool
    public let unknownNature: OSDINatureReference
    public let residualNature: OSDINatureReference
    public let activeUnknownIndex: Int?
    /// False only when the raw row has no reactive residual storage; sampled Q/C do not determine this fact.
    public let hasReactiveResidual: Bool

    public init(index: Int, name: String, units: String, residualUnits: String,
                isFlow: Bool, unknownNature: OSDINatureReference,
                residualNature: OSDINatureReference, activeUnknownIndex: Int?,
                hasReactiveResidual: Bool = true) throws(OSDIContractError) {
        guard index >= 0, !name.isEmpty, !name.utf8.contains(0),
              activeUnknownIndex.map({ $0 >= 0 }) ?? true else { throw .invalidTopology }
        self.index = index
        self.name = name
        self.units = units
        self.residualUnits = residualUnits
        self.isFlow = isFlow
        self.unknownNature = unknownNature
        self.residualNature = residualNature
        self.activeUnknownIndex = activeUnknownIndex
        self.hasReactiveResidual = hasReactiveResidual
    }
}
