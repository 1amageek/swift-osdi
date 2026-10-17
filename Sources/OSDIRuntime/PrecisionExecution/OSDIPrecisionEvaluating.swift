import OSDIContracts

public protocol OSDIPrecisionEvaluating: Sendable {
    func evaluate<Result>(session: inout OSDIPrecisionSession,
        candidateHigh: borrowing Span<Double>, candidateLow: borrowing Span<Double>,
        referenceHigh: borrowing Span<Double>, referenceLow: borrowing Span<Double>,
        candidateContext: OSDIPrecisionContext, referenceContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionEvaluationView) -> Result) throws(OSDIError) -> Result
}
