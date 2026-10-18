import OSDIContracts

public protocol OSDIPrecisionDecisionCoverEvaluating: Sendable {
    func constructDecisionCover<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionDecisionCoverEvaluationView) -> Result) throws(OSDIPrecisionDecisionCoverError) -> Result
}
