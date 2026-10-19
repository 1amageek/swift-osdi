import Foundation
import Synchronization

/// Exact managed payload; allocator rounding and metadata are outside this contract.
internal final class OSDIPrecisionSnapshotBuffer: Sendable {
    // Unsafe contract: one owner frees the calloc block exactly once. Its byte count
    // is checked/admitted before allocation; calloc initializes the entire range.
    // Alignment is malloc alignment; ABI records are always loaded/stored unaligned.
    // Pointers are borrowed synchronously while this owner is retained, never across
    // await. State is immutable after construction and no callback runs under Mutex.
    private struct State { let pointer: UnsafeMutableRawPointer? }
    private let state: Mutex<State>
    private let reservation: any Sendable
    let count: Int

    init(count: Int, control: any OSDIPrecisionSnapshotControlling) throws {
        guard count >= 0 else { throw OSDIError.resourceLimit }
        try control.checkpoint(workUnits: UInt64(count))
        reservation = try control.reserveStorage(byteCount: UInt64(count))
        self.count = count
        if count == 0 { state = Mutex(State(pointer: nil)) }
        else {
            guard let pointer = calloc(1, count) else { throw OSDIError.allocationFailed(byteCount: UInt64(count)) }
            state = Mutex(State(pointer: pointer))
        }
    }
    func withBytes<Result>(_ body: (UnsafeRawBufferPointer) throws -> Result) rethrows -> Result {
        let pointer = state.withLock { $0.pointer }
        return try withExtendedLifetime(self) { try body(UnsafeRawBufferPointer(start: pointer, count: count)) }
    }
    /// Construction-only access; the caller must publish this owner only after all writes.
    func withMutableBytes<Result>(_ body: (UnsafeMutableRawBufferPointer) throws -> Result) rethrows -> Result {
        let pointer = state.withLock { $0.pointer }
        return try withExtendedLifetime(self) { try body(UnsafeMutableRawBufferPointer(start: pointer, count: count)) }
    }
    deinit { free(state.withLock { $0.pointer }) }
}
