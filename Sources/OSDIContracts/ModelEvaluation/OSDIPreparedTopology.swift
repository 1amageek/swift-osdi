/// Dense active unknowns include every internal variable surviving setup-time collapse.
public struct OSDIPreparedTopology: Sendable, Equatable {
    public let nodes: [OSDINodeInfo]
    public let activeUnknownCount: Int
    public let stateCount: Int

    public init(nodes: [OSDINodeInfo], activeUnknownCount: Int, stateCount: Int) throws(OSDIContractError) {
        guard activeUnknownCount >= 0, activeUnknownCount <= nodes.count, stateCount >= 0 else {
            throw .invalidTopology
        }
        var active = Set<Int>()
        for (index, node) in nodes.enumerated() {
            guard node.index == index else { throw .invalidTopology }
            if let coordinate = node.activeUnknownIndex {
                guard coordinate < activeUnknownCount else { throw .invalidTopology }
                active.insert(coordinate)
            }
        }
        guard active.count == activeUnknownCount else { throw .invalidTopology }
        self.nodes = nodes
        self.activeUnknownCount = activeUnknownCount
        self.stateCount = stateCount
    }
}
