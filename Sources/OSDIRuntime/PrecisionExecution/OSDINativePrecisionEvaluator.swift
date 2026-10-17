import OSDIContracts

public struct OSDINativePrecisionEvaluator: OSDIPrecisionEvaluating {
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
}
