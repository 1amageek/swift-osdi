/// Caller-selected native workspace and bounded diagnostic/string admission limits.
public struct OSDIEvaluationLimits: Sendable, Equatable {
    public let maximumWorkspaceBytes: UInt64
    public let maximumDiagnosticCount: UInt32
    public let maximumDiagnosticBytes: UInt64
    public let maximumStringBytes: UInt64

    public init(maximumWorkspaceBytes: UInt64, maximumDiagnosticCount: UInt32,
                maximumDiagnosticBytes: UInt64, maximumStringBytes: UInt64) throws(OSDIContractError) {
        guard maximumWorkspaceBytes > 0, maximumDiagnosticCount > 0,
              maximumDiagnosticBytes > 0, maximumStringBytes > 0,
              maximumDiagnosticBytes <= maximumWorkspaceBytes,
              maximumStringBytes <= maximumWorkspaceBytes else { throw .invalidEvaluationLimits }
        self.maximumWorkspaceBytes = maximumWorkspaceBytes
        self.maximumDiagnosticCount = maximumDiagnosticCount
        self.maximumDiagnosticBytes = maximumDiagnosticBytes
        self.maximumStringBytes = maximumStringBytes
    }
}
