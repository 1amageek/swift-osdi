/// Descriptor-coordinate source identity. A nil negative endpoint denotes ground.
public struct OSDINoiseSourceInfo: Sendable, Equatable {
    public let index: Int
    public let name: String
    public let kind: OSDINoiseSourceKind
    public let positiveNodeIndex: Int
    public let negativeNodeIndex: Int?

    public init(index: Int, name: String, kind: OSDINoiseSourceKind,
                positiveNodeIndex: Int, negativeNodeIndex: Int?) throws(OSDIContractError) {
        guard index >= 0, !name.isEmpty, positiveNodeIndex >= 0,
              negativeNodeIndex.map({ $0 >= 0 }) ?? true else { throw .invalidTopology }
        self.index = index; self.name = name; self.kind = kind
        self.positiveNodeIndex = positiveNodeIndex; self.negativeNodeIndex = negativeNodeIndex
    }
}
