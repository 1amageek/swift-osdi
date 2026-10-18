/// Explicit session-cumulative source-step and fixed tape/guard storage policy.
public struct OSDIPrecisionChartLimits: Sendable, Equatable {
    public let maximumSteps: UInt64
    public let maximumTapeEntries: UInt64
    public let maximumGuardRecords: UInt64
    public init(maximumSteps: UInt64, maximumTapeEntries: UInt64, maximumGuardRecords: UInt64) throws(OSDIContractError) {
        guard maximumSteps > 0, maximumTapeEntries > 0, maximumGuardRecords > 0,
              maximumTapeEntries <= UInt64(Int.max), maximumGuardRecords <= UInt64(Int.max) else { throw .invalidLimits }
        self.maximumSteps = maximumSteps; self.maximumTapeEntries = maximumTapeEntries
        self.maximumGuardRecords = maximumGuardRecords
    }
}
