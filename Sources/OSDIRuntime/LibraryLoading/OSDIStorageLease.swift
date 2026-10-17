import OSDIContracts

/// Stored after native blocks so their owner releases this lease after freeing them.
internal struct OSDIStorageLease: ~Copyable {
    let descriptor: OSDIDescriptorInfo
    private let owner: OSDILibraryOwner

    internal init(descriptor: OSDIDescriptorInfo, owner: OSDILibraryOwner) {
        self.descriptor = descriptor
        self.owner = owner
    }

    deinit { owner.releaseLease() }
}
