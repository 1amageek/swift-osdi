import CircuiteFoundation
import OSDIContracts

/// Shared library lifetime with immutable historical descriptor metadata.
public struct OSDILibrary: Sendable {
    public let descriptors: [OSDIDescriptorInfo]
    public let reference: ArtifactReference
    public let descriptorStride: UInt64
    internal let owner: OSDILibraryOwner
    private let limits: OSDILoadLimits

    internal init(owner: OSDILibraryOwner, descriptors: [OSDIDescriptorInfo],
                  reference: ArtifactReference, descriptorStride: UInt64, limits: OSDILoadLimits) {
        self.owner = owner
        self.descriptors = descriptors
        self.reference = reference
        self.descriptorStride = descriptorStride
        self.limits = limits
    }

    /// Retains the image until this exclusive storage has freed all native bytes.
    public func makeStorage(descriptorIndex: Int) throws(OSDIError) -> OSDIStorage {
        guard descriptors.indices.contains(descriptorIndex) else { throw .invalidRange }
        let lease = try owner.acquireLease(descriptor: descriptors[descriptorIndex])
        return try OSDIStorage(lease: consume lease, maximumByteCount: limits.maximumStorageBytes)
    }

    /// Refuses active storage; an unsuccessful close can be retried on this value.
    public func close() throws(OSDIError) {
        do { try owner.close() }
        catch {
            switch error {
            case .native, .host:
                throw .cleanupRequired(cause: error, owner: OSDIPendingLibraryCleanup(owner: owner))
            default:
                throw error
            }
        }
    }
}
