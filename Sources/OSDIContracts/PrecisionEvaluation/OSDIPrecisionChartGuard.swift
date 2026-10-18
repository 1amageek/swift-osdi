/// One conjunct in source order. Bounds do not prove simultaneous feasibility.
public struct OSDIPrecisionChartGuard: Sendable, Equatable {
    public let decision: OSDIPrecisionChartDecision
    public let opcode: UInt32
    public let a: OSDIPrecisionChartOperand
    public let b: OSDIPrecisionChartOperand
    public let formulaTruthKnown: Bool
    public let boundaryExtension: Bool
    public init(decision: OSDIPrecisionChartDecision, opcode: UInt32,
                a: OSDIPrecisionChartOperand, b: OSDIPrecisionChartOperand,
                formulaTruthKnown: Bool, boundaryExtension: Bool) {
        self.decision = decision; self.opcode = opcode; self.a = a; self.b = b
        self.formulaTruthKnown = formulaTruthKnown; self.boundaryExtension = boundaryExtension
    }
}
