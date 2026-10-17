import OSDIContracts

public protocol OSDIModelEvaluating: Sendable {
    func evaluate<Result>(model: inout OSDIPreparedModel, unknowns: borrowing Span<Double>,
                          previousState: borrowing Span<Double>,
                          _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result
    func evaluate<Result>(model: inout OSDIPreparedModel, context: OSDIEvaluationContext,
                          unknowns: borrowing Span<Double>, previousState: borrowing Span<Double>,
                          _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result
}

extension OSDIModelEvaluating {
    public func evaluate<Result>(model: inout OSDIPreparedModel, context: OSDIEvaluationContext,
                                 unknowns: borrowing Span<Double>, previousState: borrowing Span<Double>,
                                 _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result {
        throw .contract(.unsupportedEvaluationContext)
    }
}
