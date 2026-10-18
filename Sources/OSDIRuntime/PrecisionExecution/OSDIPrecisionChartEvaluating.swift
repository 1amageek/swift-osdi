import OSDIContracts

public protocol OSDIPrecisionChartEvaluating: Sendable {
    func evaluateChart<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, mode: OSDIPrecisionChartMode, tape: borrowing OSDIPrecisionChartTape?,
        _ body: (OSDIPrecisionChartEvaluationView) -> Result) throws(OSDIPrecisionChartError) -> Result
}
