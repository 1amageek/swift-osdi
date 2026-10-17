import OSDIContracts

/// Evaluates an explicit local phase without solving a circuit or committing history.
public struct OSDINativeModelEvaluator: OSDIModelEvaluating {
    public init() {}

    public func evaluate<Result>(model: inout OSDIPreparedModel, unknowns: borrowing Span<Double>,
                                 previousState: borrowing Span<Double>,
                                 _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result {
        try model.evaluate(context: .dc, unknowns: unknowns, previousState: previousState, body)
    }
    public func evaluate<Result>(model: inout OSDIPreparedModel, context: OSDIEvaluationContext,
                                 unknowns: borrowing Span<Double>, previousState: borrowing Span<Double>,
                                 _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result {
        try model.evaluate(context: context, unknowns: unknowns, previousState: previousState, body)
    }
}
