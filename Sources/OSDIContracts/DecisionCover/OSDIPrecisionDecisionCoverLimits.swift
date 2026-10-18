/// Explicit session-cumulative execution policy and fully preadmitted cover storage.
public struct OSDIPrecisionDecisionCoverLimits: Sendable, Equatable {
    public let maximumSteps: UInt64
    public let maximumGuardRecords: UInt64
    public let maximumCopiedBytes: UInt64
    public let maximumCells: UInt64
    public let maximumNodes: UInt32
    public let maximumLeaves: UInt32
    public let maximumPending: UInt32
    public let maximumDepth: UInt32
    public let maximumIntegerBits: UInt32
    public init(maximumSteps: UInt64, maximumGuardRecords: UInt64, maximumCopiedBytes: UInt64,
        maximumCells: UInt64, maximumNodes: UInt32, maximumLeaves: UInt32,
        maximumPending: UInt32, maximumDepth: UInt32, maximumIntegerBits: UInt32) throws(OSDIContractError) {
        guard maximumSteps > 0, maximumGuardRecords > 0, maximumCopiedBytes > 0, maximumCells > 0,
              maximumNodes > 0, maximumNodes < UInt32.max, maximumLeaves > 0, maximumPending > 0,
              maximumDepth > 0, maximumIntegerBits > 0, maximumIntegerBits <= 1025 else { throw .invalidLimits }
        self.maximumSteps = maximumSteps; self.maximumGuardRecords = maximumGuardRecords
        self.maximumCopiedBytes = maximumCopiedBytes; self.maximumCells = maximumCells
        self.maximumNodes = maximumNodes; self.maximumLeaves = maximumLeaves
        self.maximumPending = maximumPending; self.maximumDepth = maximumDepth; self.maximumIntegerBits = maximumIntegerBits
    }
}
