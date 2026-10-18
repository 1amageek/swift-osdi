import COSDI
import OSDIContracts

/// Scoped wire-backed guard collection. Indexing copies one value; the collection never escapes.
public struct OSDIPrecisionChartGuardCollectionView: ~Escapable {
    private let storage: Span<cs_osdi_chart_guard_v3>
    public var count: Int { storage.count }
    @_lifetime(copy storage)
    internal init(storage: Span<cs_osdi_chart_guard_v3>) { self.storage = storage }
    public subscript(index: Int) -> OSDIPrecisionChartGuard {
        let value = storage[index], decision = value.decision
        return OSDIPrecisionChartGuard(
            decision: OSDIPrecisionChartDecision(stage: decision.stage, block: decision.block,
                instruction: decision.instruction, kind: OSDIPrecisionChartDecision.Kind(rawValue: decision.kind)!,
                choice: decision.choice != 0, ordinal: decision.ordinal), opcode: value.opcode,
            a: OSDIPrecisionChartOperand(register: value.a == UInt32.max ? nil : value.a,
                kind: OSDIPrecisionChartOperand.Kind(rawValue: value.a_type)!, integerBits: value.a_integer,
                lower: value.a_lower, upper: value.a_upper),
            b: OSDIPrecisionChartOperand(register: value.b == UInt32.max ? nil : value.b,
                kind: OSDIPrecisionChartOperand.Kind(rawValue: value.b_type)!, integerBits: value.b_integer,
                lower: value.b_lower, upper: value.b_upper),
            formulaTruthKnown: value.formula_truth_known != 0, boundaryExtension: value.boundary_extension != 0)
    }
}
