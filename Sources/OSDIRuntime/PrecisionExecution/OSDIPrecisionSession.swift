import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
import Synchronization

/// Exclusive helper owner. No child wait, client callback or file I/O occurs under Mutex.
public struct OSDIPrecisionSession: ~Copyable, Sendable {
    internal enum Operation: Sendable { case point, range }
    internal struct State { var handle: OpaquePointer? }
    private let state: Mutex<State>
    private let stagingDirectory: URL?
    private let operation: Operation
    public let program: OSDIPrecisionProgram
    public let workerReference: CircuiteFoundation.ArtifactReference
    public let numericProfile: ContentDigest
    public let options: OSDIPrecisionOptions
    internal init(handle: sending OpaquePointer, stagingDirectory: URL?, program: OSDIPrecisionProgram,
                  workerReference: CircuiteFoundation.ArtifactReference, numericProfile: ContentDigest,
                  options: OSDIPrecisionOptions, operation: Operation = .point) {
        state = Mutex(State(handle: handle)); self.stagingDirectory = stagingDirectory
        self.operation = operation
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
        guard operation == .point else { throw .invalidRange }
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
    internal mutating func evaluateRange<Result>(lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, _ body: (OSDIPrecisionRangeEvaluationView) -> Result) throws(OSDIError) -> Result {
        guard let handle = state.withLock({ $0.handle }) else { throw .closed }
        guard operation == .range else { throw .invalidRange }
        let count = program.topology.activeUnknownCount
        guard lowerHigh.count == count, lowerLow.count == count, upperHigh.count == count,
              upperLow.count == count, lowerContext.mode == upperContext.mode else { throw .invalidRange }
        var lower = cs_osdi_precision_context_v1(mode: lowerContext.mode.rawValue, reserved: 0,
            time_high: lowerContext.timeHigh, time_low: lowerContext.timeLow)
        var upper = cs_osdi_precision_context_v1(mode: upperContext.mode.rawValue, reserved: 0,
            time_high: upperContext.timeHigh, time_low: upperContext.timeLow)
        var native = cs_osdi_range_result_view(), issue = cs_osdi_precision_process_issue()
        let status = lowerHigh.withUnsafeBufferPointer { lh in lowerLow.withUnsafeBufferPointer { ll in
            upperHigh.withUnsafeBufferPointer { uh in upperLow.withUnsafeBufferPointer { ul in
                cs_osdi_range_process_evaluate(handle, lh.baseAddress, ll.baseAddress, uh.baseAddress, ul.baseAddress,
                    UInt64(count), &lower, &upper, precisionBits, cs_osdi_precision_swift_task_cancelled,
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
        let (matrix, overflow) = nodes.multipliedReportingOverflow(by: nodes)
        guard !overflow, native.raw_node_count == nodes, native.program_has_time_input <= 1,
              nodes == 0 || (native.f_lower != nil && native.f_upper != nil && native.q_lower != nil && native.q_upper != nil),
              matrix == 0 || (native.g_lower != nil && native.g_upper != nil && native.c_lower != nil && native.c_upper != nil) else {
            do { try close() }
            catch { throw .cleanup(primary: .identityMismatch, secondary: error) }
            throw .identityMismatch
        }
        // C validates initialized regions, outward endpoints and transport geometry.
        // The exclusive session retains the mapping through the synchronous client borrow.
        return body(OSDIPrecisionRangeEvaluationView(
            fLower: nodes == 0 ? Span() : Span(_unsafeStart: native.f_lower!, count: nodes),
            fUpper: nodes == 0 ? Span() : Span(_unsafeStart: native.f_upper!, count: nodes),
            qLower: nodes == 0 ? Span() : Span(_unsafeStart: native.q_lower!, count: nodes),
            qUpper: nodes == 0 ? Span() : Span(_unsafeStart: native.q_upper!, count: nodes),
            gLower: matrix == 0 ? Span() : Span(_unsafeStart: native.g_lower!, count: matrix),
            gUpper: matrix == 0 ? Span() : Span(_unsafeStart: native.g_upper!, count: matrix),
            cLower: matrix == 0 ? Span() : Span(_unsafeStart: native.c_lower!, count: matrix),
            cUpper: matrix == 0 ? Span() : Span(_unsafeStart: native.c_upper!, count: matrix),
            maximumTimestep: native.has_maximum_timestep == 0 ? nil : native.maximum_timestep,
            usedPrecisionBits: native.used_precision_bits, completedWorkUnits: native.completed_work_units,
            requestSequence: native.request_sequence, programHasTimeInput: native.program_has_time_input != 0))
    }
}
