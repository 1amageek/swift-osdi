import COSDI
import OSDIContracts

/// Scoped wire-backed tree. Indexing copies one bounded value with inline integer storage.
public struct OSDIPrecisionDecisionCoverNodeCollectionView: ~Escapable {
    private let storage: Span<cs_osdi_cover_node_v4>
    public var count: Int { storage.count }
    @_lifetime(copy storage)
    internal init(storage: Span<cs_osdi_cover_node_v4>) { self.storage = storage }
    public subscript(index: Int) -> OSDIPrecisionDecisionCoverNode {
        let value = storage[index]
        let kind = OSDIPrecisionDecisionCoverNode.Kind(rawValue: value.kind)!
        return OSDIPrecisionDecisionCoverNode(parent: value.parent == UInt32.max ? nil : value.parent,
            depth: value.depth, kind: kind, firstChild: value.first_child == UInt32.max ? nil : value.first_child,
            childCount: value.child_count, leaf: value.leaf == UInt32.max ? nil : value.leaf,
            incoming: value.parent == UInt32.max ? nil : Self.decision(value.incoming),
            event: kind == .decision ? Self.event(value.event) : nil)
    }
    private static func integer(_ value: cs_osdi_cover_integer_v4) -> OSDIPrecisionCellInteger {
        let words: InlineArray<17, UInt64> = withUnsafeBytes(of: value.limbs) { bytes in
            // The checked C record is initialized and each UInt64 load is aligned
            // within this local tuple. No pointer leaves the closure.
            InlineArray { bytes.load(fromByteOffset: $0 * MemoryLayout<UInt64>.stride, as: UInt64.self) }
        }
        do { return try OSDIPrecisionCellInteger(isNegative: value.negative != 0, magnitude: words.span.extracting(0..<Int(value.count))) }
        catch { preconditionFailure("Validated cover contains a malformed integer capsule") }
    }
    private static func decision(_ value: cs_osdi_cover_decision_v4) -> OSDIPrecisionSourceDecision {
        let s = value.site
        return OSDIPrecisionSourceDecision(stage: s.stage, block: s.block,
            instruction: s.instruction == UInt32.max ? nil : s.instruction,
            kind: OSDIPrecisionSourceDecision.Kind(rawValue: s.kind)!, choice: s.choice,
            ordinal: s.ordinal, opcode: value.opcode, cell: integer(value.cell))
    }
    private static func event(_ value: cs_osdi_cover_event_v4) -> OSDIPrecisionDecisionEvent {
        let g = value.operands
        return OSDIPrecisionDecisionEvent(decision: decision(value.decision),
            a: OSDIPrecisionChartOperand(register: g.a_type == 0 || g.a == UInt32.max ? nil : g.a,
                kind: OSDIPrecisionChartOperand.Kind(rawValue: g.a_type)!, integerBits: g.a_integer, lower: g.a_lower, upper: g.a_upper),
            b: OSDIPrecisionChartOperand(register: g.b_type == 0 || g.b == UInt32.max ? nil : g.b,
                kind: OSDIPrecisionChartOperand.Kind(rawValue: g.b_type)!, integerBits: g.b_integer, lower: g.b_lower, upper: g.b_upper),
            allowedMask: value.allowed_mask, cellCount: value.cell_count,
            firstCell: integer(value.first_cell), lastCell: integer(value.last_cell))
    }
}
