import OSDIContracts

public protocol OSDIPrecisionConstitutiveEvaluating: Sendable {
    func constructConstitutiveIdentities<Result>(session: inout OSDIPrecisionSession,
        lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext, precisionBits: UInt32,
        _ body: (OSDIPrecisionConstitutiveEvaluationView) -> Result) throws(OSDIPrecisionConstitutiveError) -> Result
}
