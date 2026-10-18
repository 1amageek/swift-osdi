/// Exact source occurrence identity, including repeated loop visits.
public struct OSDIPrecisionChartDecision: Sendable, Equatable {
    public enum Kind: UInt32, Sendable { case predicate = 1, branch = 2, select = 3 }
    public let stage: UInt32
    public let block: UInt32
    public let instruction: UInt32
    public let kind: Kind
    public let choice: Bool
    public let ordinal: UInt64
    public init(stage: UInt32, block: UInt32, instruction: UInt32, kind: Kind, choice: Bool, ordinal: UInt64) {
        self.stage = stage; self.block = block; self.instruction = instruction
        self.kind = kind; self.choice = choice; self.ordinal = ordinal
    }
}
