import OSDIContracts

public struct OSDINativePrecisionEvaluator: OSDIPrecisionEvaluating, OSDIPrecisionRangeEvaluating, OSDIPrecisionChartEvaluating {
    public init() {}
    public func evaluate<Result>(session: inout OSDIPrecisionSession,
        candidateHigh: borrowing Span<Double>, candidateLow: borrowing Span<Double>,
        referenceHigh: borrowing Span<Double>, referenceLow: borrowing Span<Double>,
        candidateContext: OSDIPrecisionContext, referenceContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionEvaluationView) -> Result) throws(OSDIError) -> Result {
        try session.evaluate(candidateHigh: candidateHigh, candidateLow: candidateLow,
            referenceHigh: referenceHigh, referenceLow: referenceLow,
            candidateContext: candidateContext, referenceContext: referenceContext, precisionBits: precisionBits, body)
    }
    public func evaluateRange<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionRangeEvaluationView) -> Result) throws(OSDIError) -> Result {
        try session.evaluateRange(lowerHigh: lowerHigh, lowerLow: lowerLow,
            upperHigh: upperHigh, upperLow: upperLow,
            lowerContext: lowerContext, upperContext: upperContext, precisionBits: precisionBits, body)
    }
    public func evaluateChart<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, mode: OSDIPrecisionChartMode, tape: borrowing OSDIPrecisionChartTape?,
        _ body: (OSDIPrecisionChartEvaluationView) -> Result) throws(OSDIPrecisionChartError) -> Result {
        try session.evaluateChart(lowerHigh: lowerHigh, lowerLow: lowerLow, upperHigh: upperHigh, upperLow: upperLow,
            lowerContext: lowerContext, upperContext: upperContext, precisionBits: precisionBits, mode: mode, tape: tape, body)
    }
}
