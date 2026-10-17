/// Caller-owned resource policy; no fallback or inferred precision is admitted.
public struct OSDIPrecisionOptions: Sendable, Equatable {
    public let maximumProgramBytes: UInt64
    public let maximumMappingBytes: UInt64
    public let arenaBytes: UInt64
    public let maximumWorkUnits: UInt64
    public let minimumPrecisionBits: UInt32
    public let maximumPrecisionBits: UInt32
    public let startupTimeoutNanoseconds: UInt64
    public let evaluationTimeoutNanoseconds: UInt64
    public let cleanupTimeoutNanoseconds: UInt64
    public let pollNanoseconds: UInt64
    public init(maximumProgramBytes: UInt64, maximumMappingBytes: UInt64, arenaBytes: UInt64,
                maximumWorkUnits: UInt64, minimumPrecisionBits: UInt32, maximumPrecisionBits: UInt32,
                startupTimeoutNanoseconds: UInt64, evaluationTimeoutNanoseconds: UInt64,
                cleanupTimeoutNanoseconds: UInt64, pollNanoseconds: UInt64) throws(OSDIContractError) {
        guard maximumProgramBytes > 0, maximumProgramBytes <= UInt64(Int.max),
              maximumMappingBytes > 0, maximumMappingBytes <= UInt64(Int.max), arenaBytes > 0,
              maximumWorkUnits > 0, minimumPrecisionBits >= 53, maximumPrecisionBits >= minimumPrecisionBits,
              startupTimeoutNanoseconds > 0, evaluationTimeoutNanoseconds > 0,
              cleanupTimeoutNanoseconds > 0, pollNanoseconds > 0 else { throw .invalidLimits }
        self.maximumProgramBytes = maximumProgramBytes; self.maximumMappingBytes = maximumMappingBytes
        self.arenaBytes = arenaBytes; self.maximumWorkUnits = maximumWorkUnits
        self.minimumPrecisionBits = minimumPrecisionBits; self.maximumPrecisionBits = maximumPrecisionBits
        self.startupTimeoutNanoseconds = startupTimeoutNanoseconds; self.evaluationTimeoutNanoseconds = evaluationTimeoutNanoseconds
        self.cleanupTimeoutNanoseconds = cleanupTimeoutNanoseconds; self.pollNanoseconds = pollNanoseconds
    }
}
