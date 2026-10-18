/// Exact source occurrence. Cell selections use `cell`; other choices use the source kind.
public struct OSDIPrecisionSourceDecision: Sendable, Equatable {
    public enum Kind: UInt32, Sendable {
        case predicate = 1, branch = 2, select = 3, relation = 4, sign = 5, integerCell = 6
    }
    public let stage: UInt32
    public let block: UInt32
    public let instruction: UInt32?
    public let kind: Kind
    /// Boolean 0/1 or relation/sign less=1, equal=2, greater=3. An ambiguous event template has no selected choice.
    public let choice: UInt32
    public let ordinal: UInt64
    public let opcode: UInt32
    public let cell: OSDIPrecisionCellInteger
    public init(stage: UInt32, block: UInt32, instruction: UInt32?, kind: Kind, choice: UInt32,
        ordinal: UInt64, opcode: UInt32, cell: OSDIPrecisionCellInteger) {
        self.stage = stage; self.block = block; self.instruction = instruction; self.kind = kind
        self.choice = choice; self.ordinal = ordinal; self.opcode = opcode; self.cell = cell
    }
}
