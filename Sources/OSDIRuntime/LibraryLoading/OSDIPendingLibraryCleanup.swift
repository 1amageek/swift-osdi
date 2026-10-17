import COSDI
import Foundation
import Synchronization

/// A cleanup-only image owner returned when an unpublished load cannot unload.
public final class OSDIPendingLibraryCleanup: Sendable {
    private struct State {
        var handle: OpaquePointer?
        var closing = false
        var closed = false
    }
    private let state: Mutex<State>
    private let stagingDirectory: URL?
    private let delegatedOwner: OSDILibraryOwner?

    internal init(handle: sending OpaquePointer, stagingDirectory: URL) {
        state = Mutex(State(handle: handle))
        self.stagingDirectory = stagingDirectory
        delegatedOwner = nil
    }

    internal init(owner: OSDILibraryOwner) {
        state = Mutex(State(handle: nil))
        stagingDirectory = nil
        delegatedOwner = owner
    }

    /// Retries native unload and private-file removal without exposing metadata.
    public func close() throws(OSDIError) {
        do { try closeRaw() }
        catch {
            switch error {
            case .native, .host:
                throw .cleanupRequired(cause: error, owner: self)
            default:
                throw error
            }
        }
    }

    private func closeRaw() throws(OSDIError) {
        if let delegatedOwner {
            try delegatedOwner.close()
            return
        }
        let admission: (closed: Bool, handle: OpaquePointer?) = try state.withLock { state throws(OSDIError) in
            if state.closed { return (true, nil) }
            guard !state.closing else { throw .libraryInUse }
            state.closing = true
            return (false, state.handle)
        }
        if admission.closed { return }
        if let handle = admission.handle {
            var error = cs_osdi_error()
            guard cs_osdi_close(handle, &error) == 0 else {
                state.withLock { $0.closing = false }
                throw .native(NativeIssueBridge.issue(error))
            }
            state.withLock { $0.handle = nil }
        }
        guard let stagingDirectory else {
            preconditionFailure("Native OSDI cleanup is missing its private directory")
        }
        do { try FileManager.default.removeItem(at: stagingDirectory) }
        catch {
            state.withLock { $0.closing = false }
            throw .host(operation: "remove staging directory", underlying: error)
        }
        state.withLock { $0.closed = true; $0.closing = false }
    }

    deinit {
        do { try closeRaw() }
        catch { preconditionFailure("Unreleased OSDI pending cleanup: \(error)") }
    }
}
