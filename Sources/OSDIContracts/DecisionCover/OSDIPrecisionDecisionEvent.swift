/// Checked local partition. These bounds alone do not establish joint guard feasibility.
public struct OSDIPrecisionDecisionEvent: Sendable, Equatable {
    public let decision: OSDIPrecisionSourceDecision
    public let a: OSDIPrecisionChartOperand
    public let b: OSDIPrecisionChartOperand
    public let allowedMask: UInt32
    public let cellCount: UInt64
    public let firstCell: OSDIPrecisionCellInteger
    public let lastCell: OSDIPrecisionCellInteger
    public init(decision: OSDIPrecisionSourceDecision, a: OSDIPrecisionChartOperand, b: OSDIPrecisionChartOperand,
        allowedMask: UInt32, cellCount: UInt64, firstCell: OSDIPrecisionCellInteger, lastCell: OSDIPrecisionCellInteger) {
        self.decision = decision; self.a = a; self.b = b; self.allowedMask = allowedMask
        self.cellCount = cellCount; self.firstCell = firstCell; self.lastCell = lastCell
    }
}
