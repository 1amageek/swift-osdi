/// A foreign nature-reference tag and index, preserved without guessing physical scaling.
public struct OSDINatureReference: Sendable, Equatable {
    public let kind: UInt32
    public let index: UInt32

    public init(kind: UInt32, index: UInt32) throws(OSDIContractError) {
        guard kind <= 3 else { throw .invalidTopology }
        self.kind = kind
        self.index = index
    }
}
