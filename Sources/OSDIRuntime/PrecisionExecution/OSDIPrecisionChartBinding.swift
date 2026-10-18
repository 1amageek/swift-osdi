import CircuiteFoundation
import OSDIContracts

/// Exact constitutive authority and raw coordinate order, without a circuit projection claim.
public struct OSDIPrecisionChartBinding: Sendable, Equatable {
    public let modelReference: ArtifactReference
    public let descriptorIndex: UInt32
    public let topology: OSDIPreparedTopology
    public let programDigest: ContentDigest
    public let setupDigest: ContentDigest
    public let semanticProfile: ContentDigest
    public init(program: OSDIPrecisionProgram) {
        modelReference = program.modelReference; descriptorIndex = program.descriptorIndex
        topology = program.topology; programDigest = program.programDigest
        setupDigest = program.setupDigest; semanticProfile = program.semanticProfile
    }
}
