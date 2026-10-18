/// One bounded record copied from a scoped complete tree; a value alone grants no completeness authority.
public struct OSDIPrecisionDecisionCoverNode: Sendable, Equatable {
    public enum Kind: UInt32, Sendable { case decision = 1, terminal = 2 }
    public let parent: UInt32?
    public let depth: UInt32
    public let kind: Kind
    public let firstChild: UInt32?
    public let childCount: UInt32
    public let leaf: UInt32?
    public let incoming: OSDIPrecisionSourceDecision?
    public let event: OSDIPrecisionDecisionEvent?
    public init(parent: UInt32?, depth: UInt32, kind: Kind, firstChild: UInt32?, childCount: UInt32,
        leaf: UInt32?, incoming: OSDIPrecisionSourceDecision?, event: OSDIPrecisionDecisionEvent?) {
        self.parent = parent; self.depth = depth; self.kind = kind; self.firstChild = firstChild
        self.childCount = childCount; self.leaf = leaf; self.incoming = incoming; self.event = event
    }
}
