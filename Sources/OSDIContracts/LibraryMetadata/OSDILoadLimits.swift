/// Caller-selected bounds for native descriptor admission and one storage owner.
public struct OSDILoadLimits: Sendable, Equatable {
    public let maximumDescriptors: UInt32
    public let maximumMetadataBytes: UInt64
    /// Per-descriptor model/instance bytes plus advertised Jacobian double-array capacities.
    /// Admission checks this bound without allocating Jacobian arrays; it is not an aggregate memory cap.
    public let maximumStorageBytes: UInt64

    public init(maximumDescriptors: UInt32, maximumMetadataBytes: UInt64,
                maximumStorageBytes: UInt64) throws(OSDIContractError) {
        guard maximumDescriptors > 0, maximumMetadataBytes > 0,
              maximumStorageBytes > 0 else { throw .invalidLimits }
        self.maximumDescriptors = maximumDescriptors
        self.maximumMetadataBytes = maximumMetadataBytes
        self.maximumStorageBytes = maximumStorageBytes
    }
}
