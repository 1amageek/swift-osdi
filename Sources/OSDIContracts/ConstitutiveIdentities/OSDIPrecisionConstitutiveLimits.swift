/// Explicit capability bounds for exact source-linear identity construction.
public struct OSDIPrecisionConstitutiveLimits: Sendable, Equatable {
    public let maximumNodes: UInt32
    public let maximumAtoms: UInt32
    public let maximumCoefficientBits: UInt64
    public let maximumWorkspaceBytes: UInt64
    public init(maximumNodes: UInt32, maximumAtoms: UInt32,
        maximumCoefficientBits: UInt64, maximumWorkspaceBytes: UInt64) throws(OSDIContractError) {
        guard maximumNodes > 0, maximumNodes < UInt32.max, maximumAtoms > 0, maximumAtoms < UInt32.max,
              maximumCoefficientBits > 0, maximumWorkspaceBytes > 0 else { throw .invalidLimits }
        self.maximumNodes = maximumNodes; self.maximumAtoms = maximumAtoms
        self.maximumCoefficientBits = maximumCoefficientBits; self.maximumWorkspaceBytes = maximumWorkspaceBytes
    }
}
