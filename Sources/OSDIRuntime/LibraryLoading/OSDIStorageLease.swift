import COSDI
import OSDIContracts

/// Keeps image authority alive until all dependent native storage is destroyed.
@usableFromInline
internal struct OSDIStorageLease: ~Copyable, Sendable {
    let descriptor: OSDIDescriptorInfo
    private let owner: OSDILibraryOwner
    private let evaluation: Bool

    internal init(descriptor: OSDIDescriptorInfo, owner: OSDILibraryOwner, evaluation: Bool = false) {
        self.descriptor = descriptor
        self.owner = owner
        self.evaluation = evaluation
    }

    internal func withNativeLibrary<Result>(_ body: (OpaquePointer) throws(OSDIError) -> Result) throws(OSDIError) -> Result {
        let handle = try owner.leasedHandle()
        return try body(handle)
    }

    deinit { owner.releaseLease(evaluation: evaluation) }
}
