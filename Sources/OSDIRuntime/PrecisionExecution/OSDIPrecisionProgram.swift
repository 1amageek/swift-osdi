import CircuiteFoundation
import OSDIContracts

/// Immutable cold snapshot for independent replay; no model pointer survives extraction.
public struct OSDIPrecisionProgram: Sendable {
    public let modelReference: ArtifactReference
    public let descriptorIndex: UInt32
    public let topology: OSDIPreparedTopology
    public let programBytes: [UInt8]
    public let setupBytes: [UInt8]
    public let programDigest: ContentDigest
    public let setupDigest: ContentDigest
    public let semanticProfile: ContentDigest
    internal init(modelReference: ArtifactReference, descriptorIndex: UInt32, topology: OSDIPreparedTopology,
                  programBytes: [UInt8], setupBytes: [UInt8], programDigest: ContentDigest,
                  setupDigest: ContentDigest, semanticProfile: ContentDigest) {
        self.modelReference = modelReference; self.descriptorIndex = descriptorIndex; self.topology = topology
        self.programBytes = programBytes; self.setupBytes = setupBytes; self.programDigest = programDigest
        self.setupDigest = setupDigest; self.semanticProfile = semanticProfile
    }
}
