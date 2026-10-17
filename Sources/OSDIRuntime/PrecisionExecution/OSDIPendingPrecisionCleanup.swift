import COSDI
import Foundation
import Synchronization

/// Cleanup-only authority retained until the exact child is reaped and files are removed.
public final class OSDIPendingPrecisionCleanup: Sendable {
    private struct State { var handle: OpaquePointer?; var closing = false; var closed = false }
    private let state: Mutex<State>
    private let stagingDirectory: URL?
    internal init(handle: sending OpaquePointer?, stagingDirectory: URL?) {
        state = Mutex(State(handle: handle)); self.stagingDirectory = stagingDirectory
    }
    public func close() throws(OSDIError) {
        do { try closeRaw() }
        catch { throw .precisionCleanup(cause: error, owner: self) }
    }
    private func closeRaw() throws(OSDIError) {
        let admission = try state.withLock { state throws(OSDIError) -> (Bool, OpaquePointer?) in
            if state.closed { return (true, nil) }
            guard !state.closing else { throw .libraryInUse }
            state.closing = true
            return (false, state.handle)
        }
        if admission.0 { return }
        var handle = admission.1
        if handle != nil {
            var issue = cs_osdi_precision_process_issue()
            let status = cs_osdi_precision_process_close(&handle, &issue)
            if status == 0 { state.withLock { $0.handle = nil } }
            guard status == 0 else {
                state.withLock { $0.closing = false }
                throw .precision(OSDIPrecisionNativeBridge.issue(issue))
            }
        }
        if let stagingDirectory {
            do { try FileManager.default.removeItem(at: stagingDirectory) }
            catch {
                state.withLock { $0.closing = false }
                throw .host(operation: "remove precision staging directory", underlying: error)
            }
        }
        state.withLock { $0.closed = true; $0.closing = false }
    }
    deinit {
        do { try closeRaw() }
        catch { preconditionFailure("Unreleased precision cleanup: \(error)") }
    }
}
