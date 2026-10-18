import OSDIContracts

public protocol OSDIPrecisionRangeEvaluating: Sendable {
    func evaluateRange<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionRangeEvaluationView) -> Result) throws(OSDIError) -> Result
}
