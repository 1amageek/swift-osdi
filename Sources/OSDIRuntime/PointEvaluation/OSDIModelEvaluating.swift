public protocol OSDIModelEvaluating: Sendable {
    func evaluate<Result>(model: inout OSDIPreparedModel, unknowns: borrowing Span<Double>,
                          previousState: borrowing Span<Double>,
                          _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result
}
