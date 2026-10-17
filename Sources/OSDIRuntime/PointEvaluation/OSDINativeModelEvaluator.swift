/// Evaluates the fixed local point profile without solving a circuit or committing history.
public struct OSDINativeModelEvaluator: OSDIModelEvaluating {
    public init() {}

    public func evaluate<Result>(model: inout OSDIPreparedModel, unknowns: borrowing Span<Double>,
                                 previousState: borrowing Span<Double>,
                                 _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result {
        try model.evaluate(unknowns: unknowns, previousState: previousState, body)
    }
}
