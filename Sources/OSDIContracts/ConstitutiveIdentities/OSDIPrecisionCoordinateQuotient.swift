/// Explicit correlated-domain equalities between prepared active coordinates.
/// UInt32.max denotes ground; other entries name the smallest member of a class.
public struct OSDIPrecisionCoordinateQuotient: Sendable, Equatable {
    public let representatives: [UInt32]
    private let topology: OSDIPreparedTopology
    public init(topology: OSDIPreparedTopology, representatives: [UInt32]) throws(OSDIContractError) {
        guard topology.activeUnknownCount < Int(UInt32.max), representatives.count == topology.activeUnknownCount else { throw .invalidTopology }
        for (index, representative) in representatives.enumerated() where representative != UInt32.max {
            guard representative <= index, representatives[Int(representative)] == representative else { throw .invalidTopology }
        }
        var natureByActive: [Int: OSDINodeInfo] = [:]
        var natureByClass: [UInt32: OSDINodeInfo] = [:]
        for node in topology.nodes {
            guard let active = node.activeUnknownIndex else { continue }
            if let first = natureByActive[active] {
                guard Self.sameNature(first, node) else { throw .invalidTopology }
            } else { natureByActive[active] = node }
            let representative = representatives[active]
            // Flow ownership is not inferred from coincident sampled values.
            guard !node.isFlow || representative == UInt32(active) else { throw .invalidTopology }
            guard representative != UInt32.max else { continue }
            if let first = natureByClass[representative] {
                guard Self.sameNature(first, node) else { throw .invalidTopology }
            } else { natureByClass[representative] = node }
        }
        self.topology = topology; self.representatives = representatives
    }
    public static func identity(topology: OSDIPreparedTopology) throws(OSDIContractError) -> Self {
        guard topology.activeUnknownCount < Int(UInt32.max) else { throw .invalidTopology }
        return try Self(topology: topology, representatives: (0..<topology.activeUnknownCount).map(UInt32.init))
    }
    public func validate(for topology: OSDIPreparedTopology) throws(OSDIContractError) {
        guard self.topology == topology else { throw .invalidTopology }
    }
    private static func sameNature(_ a: OSDINodeInfo, _ b: OSDINodeInfo) -> Bool {
        a.units == b.units && a.residualUnits == b.residualUnits && a.isFlow == b.isFlow &&
            a.unknownNature == b.unknownNature && a.residualNature == b.residualNature
    }
}
