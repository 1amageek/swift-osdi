import OSDIContracts

/// Independently checked complete source decision cover. No root, nonemptiness or flow authority.
public struct OSDIPrecisionDecisionCoverEvaluationView: ~Escapable {
    public let nodes: OSDIPrecisionDecisionCoverNodeCollectionView
    public let leafCount: Int
    public let binding: OSDIPrecisionChartBinding
    public let lowerHigh: Span<Double>
    public let lowerLow: Span<Double>
    public let upperHigh: Span<Double>
    public let upperLow: Span<Double>
    public let lowerContext: OSDIPrecisionContext
    public let upperContext: OSDIPrecisionContext
    public let usedPrecisionBits: UInt32
    public let completedWorkUnits: UInt64
    public let completedSteps: UInt64
    public let completedGuardRecords: UInt64
    public let completedCopiedBytes: UInt64
    public let requestSequence: UInt64
    public let programHasTimeInput: Bool
    private let leaves: Span<Double>
    private let rawNodes: Int
    private let stride: Int
    @_lifetime(copy nodes, copy lowerHigh, copy lowerLow, copy upperHigh, copy upperLow, copy leaves)
    internal init(nodes: OSDIPrecisionDecisionCoverNodeCollectionView, leafCount: Int, binding: OSDIPrecisionChartBinding,
        lowerHigh: Span<Double>, lowerLow: Span<Double>, upperHigh: Span<Double>, upperLow: Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext, leaves: Span<Double>, rawNodes: Int, stride: Int,
        usedPrecisionBits: UInt32, completedWorkUnits: UInt64, completedSteps: UInt64, completedGuardRecords: UInt64,
        completedCopiedBytes: UInt64, requestSequence: UInt64, programHasTimeInput: Bool) {
        self.nodes = nodes; self.leafCount = leafCount; self.binding = binding
        self.lowerHigh = lowerHigh; self.lowerLow = lowerLow; self.upperHigh = upperHigh; self.upperLow = upperLow
        self.lowerContext = lowerContext; self.upperContext = upperContext; self.leaves = leaves
        self.rawNodes = rawNodes; self.stride = stride; self.usedPrecisionBits = usedPrecisionBits
        self.completedWorkUnits = completedWorkUnits; self.completedSteps = completedSteps
        self.completedGuardRecords = completedGuardRecords; self.completedCopiedBytes = completedCopiedBytes
        self.requestSequence = requestSequence; self.programHasTimeInput = programHasTimeInput
    }
    public func withLeaf<Result>(at index: Int, _ body: (OSDIPrecisionDecisionCoverLeafView) -> Result) -> Result {
        precondition(index >= 0 && index < leafCount)
        return body(OSDIPrecisionDecisionCoverLeafView(storage: leaves.extracting(index*stride..<(index+1)*stride), nodes: rawNodes))
    }
}
