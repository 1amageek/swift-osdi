import COSDI
import OSDIContracts

/// Explicit caller-retained decisions. Native wire storage is created once, not per replay.
public struct OSDIPrecisionChartTape: Sendable {
    public let binding: OSDIPrecisionChartBinding
    internal let storage: [cs_osdi_chart_decision_v3]
    public var count: Int { storage.count }
    public init(binding: OSDIPrecisionChartBinding, decisions: [OSDIPrecisionChartDecision]) throws(OSDIPrecisionChartError) {
        var native: [cs_osdi_chart_decision_v3] = []
        native.reserveCapacity(decisions.count)
        for (index, decision) in decisions.enumerated() {
            guard (1...3).contains(decision.stage), decision.ordinal == UInt64(index) else { throw .runtime(.invalidRange) }
            native.append(cs_osdi_chart_decision_v3(stage: decision.stage, block: decision.block,
                instruction: decision.instruction, kind: decision.kind.rawValue, choice: decision.choice ? 1 : 0,
                reserved: 0, ordinal: decision.ordinal))
        }
        self.binding = binding; storage = native
    }
    public subscript(index: Int) -> OSDIPrecisionChartDecision {
        let value = storage[index]
        return OSDIPrecisionChartDecision(stage: value.stage, block: value.block, instruction: value.instruction,
            kind: OSDIPrecisionChartDecision.Kind(rawValue: value.kind)!, choice: value.choice != 0, ordinal: value.ordinal)
    }
}
