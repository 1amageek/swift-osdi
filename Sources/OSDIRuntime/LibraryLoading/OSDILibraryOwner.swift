import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
import Synchronization

internal final class OSDILibraryOwner: Sendable {
    private enum Lifecycle { case open, closing, closeFailed, closed }
    private struct State {
        var handle: OpaquePointer?
        var lifecycle: Lifecycle = .open
        var leaseCount = 0
        var evaluationActive = false
    }
    private let state: Mutex<State>
    private let stagingDirectory: URL

    private init(handle: sending OpaquePointer, stagingDirectory: URL) {
        state = Mutex(State(handle: handle))
        self.stagingDirectory = stagingDirectory
    }

    /// Takes ownership on entry, including every metadata rejection path.
    internal static func publish(handle: sending OpaquePointer, stagingDirectory: URL,
                                 reference: ArtifactReference, limits: OSDILoadLimits) throws(OSDIError) -> OSDILibrary {
        do {
            let count = cs_osdi_descriptor_count(handle)
            guard count > 0, count <= limits.maximumDescriptors, let capacity = Int(exactly: count) else {
                throw OSDIError.resourceLimit
            }
            var descriptors: [OSDIDescriptorInfo] = []
            descriptors.reserveCapacity(capacity)
            var nameBytes: UInt64 = 0
            for index in 0..<count {
                var info = cs_osdi_descriptor_info_record()
                var error = cs_osdi_error()
                guard cs_osdi_descriptor_info(handle, index, &info, &error) == 0 else {
                    throw OSDIError.native(NativeIssueBridge.issue(error))
                }
                let (nextNameBytes, overflow) = nameBytes.addingReportingOverflow(info.name_length)
                guard !overflow, nextNameBytes <= limits.maximumMetadataBytes,
                      let length = Int(exactly: info.name_length), length > 0,
                      let pointer = info.name else { throw OSDIError.invalidDescriptorName(index: Int(index)) }
                // COSDI has validated the image-owned range; this bounded borrow never escapes.
                let bytes = UnsafeRawBufferPointer(start: pointer, count: length)
                guard let name = String(validating: bytes, as: UTF8.self), !name.isEmpty else {
                    throw OSDIError.invalidDescriptorName(index: Int(index))
                }
                nameBytes = nextNameBytes
                guard let modelBytes = Int(exactly: info.model_bytes),
                      let instanceBytes = Int(exactly: info.instance_bytes) else { throw OSDIError.resourceLimit }
                do {
                    descriptors.append(try OSDIDescriptorInfo(index: Int(index), name: name,
                        nodeCount: Int(info.nodes), terminalCount: Int(info.terminals),
                        jacobianEntryCount: Int(info.jacobian_entries), stateCount: Int(info.states),
                        collapsibleCount: Int(info.collapsible), parameterCount: Int(info.parameters),
                        instanceParameterCount: Int(info.instance_parameters), operatingPointCount: Int(info.opvars),
                        noiseSourceCount: Int(info.noise_sources), instanceByteCount: instanceBytes, modelByteCount: modelBytes))
                } catch { throw OSDIError.contract(error) }
            }
            let stride = cs_osdi_descriptor_stride(handle)
            return OSDILibrary(owner: OSDILibraryOwner(handle: handle, stagingDirectory: stagingDirectory),
                descriptors: descriptors, reference: reference, descriptorStride: stride, limits: limits)
        } catch let primary as OSDIError {
            let cleanup = OSDIPendingLibraryCleanup(handle: handle, stagingDirectory: stagingDirectory)
            do { try cleanup.close() }
            catch { throw .pendingCleanup(primary: primary, secondary: error, owner: cleanup) }
            throw primary
        } catch {
            // All producers above are typed; preserve any future host failure without losing ownership.
            let primary = OSDIError.host(operation: "copy descriptor metadata", underlying: error)
            let cleanup = OSDIPendingLibraryCleanup(handle: handle, stagingDirectory: stagingDirectory)
            do { try cleanup.close() }
            catch { throw .pendingCleanup(primary: primary, secondary: error, owner: cleanup) }
            throw primary
        }
    }

    internal func acquireLease(descriptor: OSDIDescriptorInfo, evaluation: Bool = false) throws(OSDIError) -> OSDIStorageLease {
        try state.withLock { state throws(OSDIError) in
            guard case .open = state.lifecycle else { throw .closed }
            guard !evaluation || !state.evaluationActive else { throw .libraryInUse }
            let (next, overflow) = state.leaseCount.addingReportingOverflow(1)
            guard !overflow else { throw .resourceLimit }
            state.leaseCount = next
            if evaluation { state.evaluationActive = true }
        }
        return OSDIStorageLease(descriptor: descriptor, owner: self, evaluation: evaluation)
    }

    internal func leasedHandle() throws(OSDIError) -> OpaquePointer {
        try state.withLock { state throws(OSDIError) in
            guard case .open = state.lifecycle, state.leaseCount > 0, let handle = state.handle else { throw .closed }
            return handle
        }
    }

    internal func releaseLease(evaluation: Bool = false) {
        state.withLock { state in
            precondition(state.leaseCount > 0, "Unbalanced OSDI storage lease")
            state.leaseCount -= 1
            if evaluation {
                precondition(state.evaluationActive, "Unbalanced OSDI evaluation lease")
                state.evaluationActive = false
            }
        }
    }

    internal func close() throws(OSDIError) {
        let admission: (closed: Bool, handle: OpaquePointer?) = try state.withLock { state throws(OSDIError) in
            if case .closed = state.lifecycle { return (true, nil) }
            guard state.leaseCount == 0 else { throw .libraryInUse }
            if case .closing = state.lifecycle { throw .libraryInUse }
            state.lifecycle = .closing
            return (false, state.handle)
        }
        if admission.closed { return }
        if let handle = admission.handle {
            var error = cs_osdi_error()
            guard cs_osdi_close(handle, &error) == 0 else {
                state.withLock { $0.lifecycle = .closeFailed }
                throw .native(NativeIssueBridge.issue(error))
            }
            state.withLock { $0.handle = nil }
        }
        do { try FileManager.default.removeItem(at: stagingDirectory) }
        catch {
            state.withLock { $0.lifecycle = .closeFailed }
            throw .host(operation: "remove staging directory", underlying: error)
        }
        state.withLock { $0.lifecycle = .closed }
    }

    deinit {
        do { try close() }
        catch { preconditionFailure("Unreleased OSDI library: \(error)") }
    }
}
