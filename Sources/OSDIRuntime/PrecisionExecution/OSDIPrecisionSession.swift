import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
import Synchronization

/// Exclusive helper owner. No child wait, client callback or file I/O occurs under Mutex.
public struct OSDIPrecisionSession: ~Copyable, Sendable {
    internal struct State { var handle: OpaquePointer? }
    private let state: Mutex<State>
    private let stagingDirectory: URL?
    public let program: OSDIPrecisionProgram
    public let workerReference: CircuiteFoundation.ArtifactReference
    public let numericProfile: ContentDigest
    public let options: OSDIPrecisionOptions
    internal init(handle: sending OpaquePointer, stagingDirectory: URL?, program: OSDIPrecisionProgram,
                  workerReference: CircuiteFoundation.ArtifactReference, numericProfile: ContentDigest, options: OSDIPrecisionOptions) {
        state = Mutex(State(handle: handle)); self.stagingDirectory = stagingDirectory
        self.program = program; self.workerReference = workerReference; self.numericProfile = numericProfile; self.options = options
    }
    public mutating func close() throws(OSDIError) {
        guard let handle = state.withLock({ $0.handle.take() }) else { return }
        try OSDIPendingPrecisionCleanup(handle: handle, stagingDirectory: stagingDirectory).close()
    }
    deinit {
        if let handle = state.withLock({ $0.handle.take() }) {
            do { try OSDIPendingPrecisionCleanup(handle: handle, stagingDirectory: stagingDirectory).close() }
            catch { preconditionFailure("Unreleased precision session: \(error)") }
        }
    }
    internal mutating func evaluate<Result>(candidateHigh: borrowing Span<Double>, candidateLow: borrowing Span<Double>,
        referenceHigh: borrowing Span<Double>, referenceLow: borrowing Span<Double>,
        candidateContext: OSDIPrecisionContext, referenceContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionEvaluationView) -> Result) throws(OSDIError) -> Result {
        guard let handle = state.withLock({ $0.handle }) else { throw .closed }
        let count = program.topology.activeUnknownCount
        guard candidateHigh.count == count, candidateLow.count == count, referenceHigh.count == count,
              referenceLow.count == count else { throw .invalidRange }
        var candidate = cs_osdi_precision_context_v1(mode: candidateContext.mode.rawValue, reserved: 0,
            time_high: candidateContext.timeHigh, time_low: candidateContext.timeLow)
        var reference = cs_osdi_precision_context_v1(mode: referenceContext.mode.rawValue, reserved: 0,
            time_high: referenceContext.timeHigh, time_low: referenceContext.timeLow)
        var native = cs_osdi_precision_result_view(), issue = cs_osdi_precision_process_issue()
        let status = candidateHigh.withUnsafeBufferPointer { ch in candidateLow.withUnsafeBufferPointer { cl in
            referenceHigh.withUnsafeBufferPointer { rh in referenceLow.withUnsafeBufferPointer { rl in
                cs_osdi_precision_process_evaluate(handle, ch.baseAddress, cl.baseAddress, rh.baseAddress, rl.baseAddress,
                    UInt64(count), &candidate, &reference, precisionBits, cs_osdi_precision_swift_task_cancelled,
                    nil, &native, &issue)
            }}
        }}
        guard status == 0 else {
            let primary = OSDIError.precision(OSDIPrecisionNativeBridge.issue(issue))
            do { try close() }
            catch { throw .cleanup(primary: primary, secondary: error) }
            throw primary
        }
        let nodes = program.topology.nodes.count
        guard native.raw_node_count == nodes else {
            do { try close() }
            catch { throw .cleanup(primary: .identityMismatch, secondary: error) }
            throw .identityMismatch
        }
        let matrix = nodes * nodes
        // C validates alignment, initialized regions, finite enclosure data and geometry.
        // The exclusive session retains the mapping through the synchronous client borrow.
        return body(OSDIPrecisionEvaluationView(
            fHigh: Span(_unsafeStart: native.f_high!, count: nodes), fLow: Span(_unsafeStart: native.f_low!, count: nodes),
            fRadius: Span(_unsafeStart: native.f_radius!, count: nodes),
            deltaQHigh: Span(_unsafeStart: native.delta_q_high!, count: nodes), deltaQLow: Span(_unsafeStart: native.delta_q_low!, count: nodes),
            deltaQRadius: Span(_unsafeStart: native.delta_q_radius!, count: nodes),
            g: Span(_unsafeStart: native.g!, count: matrix), c: Span(_unsafeStart: native.c!, count: matrix),
            maximumTimestep: native.has_maximum_timestep == 0 ? nil : native.maximum_timestep,
            usedPrecisionBits: native.used_precision_bits, completedWorkUnits: native.completed_work_units,
            requestSequence: native.request_sequence))
    }
}
