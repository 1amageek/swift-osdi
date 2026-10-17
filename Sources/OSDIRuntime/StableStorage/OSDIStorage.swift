import COSDI
import OSDIContracts

/// Exclusive, stable zeroed storage. Destroying this value frees its bytes before its library lease.
public struct OSDIStorage: ~Copyable {
    private let lease: OSDIStorageLease
    private let modelMemory: UnsafeMutablePointer<UInt8>?
    private let instanceMemory: UnsafeMutablePointer<UInt8>?

    public var descriptor: OSDIDescriptorInfo { lease.descriptor }

    internal init(
        lease: consuming OSDIStorageLease,
        maximumByteCount: UInt64
    ) throws(OSDIError) {
        let descriptor = lease.descriptor
        guard descriptor.modelByteCount >= 0, descriptor.instanceByteCount >= 0 else {
            throw .invalidRange
        }
        let modelByteCount = UInt64(descriptor.modelByteCount)
        let instanceByteCount = UInt64(descriptor.instanceByteCount)
        let (combinedByteCount, overflow) = modelByteCount.addingReportingOverflow(instanceByteCount)
        guard !overflow, combinedByteCount <= maximumByteCount else { throw .resourceLimit }

        let modelAllocation: UnsafeMutableRawPointer?
        if modelByteCount == 0 {
            modelAllocation = nil
        } else {
            guard let allocation = cs_osdi_allocate_zeroed(modelByteCount) else {
                throw .allocationFailed(byteCount: modelByteCount)
            }
            modelAllocation = allocation
        }
        let instanceAllocation: UnsafeMutableRawPointer?
        if instanceByteCount == 0 {
            instanceAllocation = nil
        } else {
            guard let allocation = cs_osdi_allocate_zeroed(instanceByteCount) else {
                cs_osdi_free(modelAllocation)
                throw .allocationFailed(byteCount: instanceByteCount)
            }
            instanceAllocation = allocation
        }

        // calloc owns initialized zero bytes; bind each complete block once to alignment-one UInt8.
        // This noncopyable owner frees both blocks exactly once before its stored lease is destroyed.
        modelMemory = modelAllocation?.bindMemory(to: UInt8.self, capacity: descriptor.modelByteCount)
        instanceMemory = instanceAllocation?.bindMemory(to: UInt8.self, capacity: descriptor.instanceByteCount)
        self.lease = consume lease
    }

    deinit {
        cs_osdi_free(modelMemory)
        cs_osdi_free(instanceMemory)
    }

    /// The byte span borrows this exclusive storage only for the synchronous callback.
    public mutating func withModelBytes<Result, Failure: Error>(
        _ body: (inout MutableSpan<UInt8>) throws(Failure) -> Result
    ) throws(Failure) -> Result {
        if let modelMemory {
            var bytes = MutableSpan(_unsafeStart: modelMemory, count: lease.descriptor.modelByteCount)
            return try body(&bytes)
        }
        var bytes = MutableSpan<UInt8>()
        return try body(&bytes)
    }

    /// The byte span borrows this exclusive storage only for the synchronous callback.
    public mutating func withInstanceBytes<Result, Failure: Error>(
        _ body: (inout MutableSpan<UInt8>) throws(Failure) -> Result
    ) throws(Failure) -> Result {
        if let instanceMemory {
            var bytes = MutableSpan(_unsafeStart: instanceMemory, count: lease.descriptor.instanceByteCount)
            return try body(&bytes)
        }
        var bytes = MutableSpan<UInt8>()
        return try body(&bytes)
    }
}
