import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
import Synchronization

/// Exclusive helper owner. No child wait, client callback or file I/O occurs under Mutex.
public struct OSDIPrecisionSession: ~Copyable, Sendable {
    internal enum Operation: Sendable { case point, range, chart }
    internal struct State { var handle: OpaquePointer? }
    private let state: Mutex<State>
    private let stagingDirectory: URL?
    private let operation: Operation
    public let program: OSDIPrecisionProgram
    public let workerReference: CircuiteFoundation.ArtifactReference
    public let numericProfile: ContentDigest
    public let options: OSDIPrecisionOptions
    public let chartLimits: OSDIPrecisionChartLimits?
    public var chartBinding: OSDIPrecisionChartBinding { OSDIPrecisionChartBinding(program: program) }
    internal init(handle: sending OpaquePointer, stagingDirectory: URL?, program: OSDIPrecisionProgram,
                  workerReference: CircuiteFoundation.ArtifactReference, numericProfile: ContentDigest,
                  options: OSDIPrecisionOptions, operation: Operation = .point, chartLimits: OSDIPrecisionChartLimits? = nil) {
        state = Mutex(State(handle: handle)); self.stagingDirectory = stagingDirectory
        self.operation = operation; self.chartLimits = chartLimits
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
    internal mutating func evaluateChart<Result>(lowerHigh: borrowing Span<Double>, lowerLow: borrowing Span<Double>,
        upperHigh: borrowing Span<Double>, upperLow: borrowing Span<Double>,
        lowerContext: OSDIPrecisionContext, upperContext: OSDIPrecisionContext,
        precisionBits: UInt32, mode: OSDIPrecisionChartMode, tape: borrowing OSDIPrecisionChartTape?,
        _ body: (OSDIPrecisionChartEvaluationView) -> Result) throws(OSDIPrecisionChartError) -> Result {
        guard let handle = state.withLock({ $0.handle }) else { throw .runtime(.closed) }
        guard operation == .chart, let chartLimits else { throw .runtime(.invalidRange) }
        let count = program.topology.activeUnknownCount
        guard lowerHigh.count == count, lowerLow.count == count, upperHigh.count == count,
              upperLow.count == count, lowerContext.mode == upperContext.mode,
              (mode == .record ? tape == nil : tape != nil) else { throw .runtime(.invalidRange) }
        // Optional pattern matching consumes its operand. Retain only the immutable
        // COW handle through this call; the native record buffer is never materialized.
        let retainedTape = copy tape
        if let tape = retainedTape {
            guard tape.binding == chartBinding else { throw .runtime(.identityMismatch) }
            guard UInt64(tape.count) <= chartLimits.maximumTapeEntries else { throw .runtime(.resourceLimit) }
        }
        var lower = cs_osdi_precision_context_v1(mode: lowerContext.mode.rawValue, reserved: 0,
            time_high: lowerContext.timeHigh, time_low: lowerContext.timeLow)
        var upper = cs_osdi_precision_context_v1(mode: upperContext.mode.rawValue, reserved: 0,
            time_high: upperContext.timeHigh, time_low: upperContext.timeLow)
        var native = cs_osdi_chart_result_view(), issue = cs_osdi_precision_process_issue()
        func invoke(_ decisions: UnsafeBufferPointer<cs_osdi_chart_decision_v3>) -> Int32 {
            lowerHigh.withUnsafeBufferPointer { lh in lowerLow.withUnsafeBufferPointer { ll in
                upperHigh.withUnsafeBufferPointer { uh in upperLow.withUnsafeBufferPointer { ul in
                    cs_osdi_chart_process_evaluate(handle, lh.baseAddress, ll.baseAddress, uh.baseAddress, ul.baseAddress,
                        UInt64(count), &lower, &upper, precisionBits, mode.rawValue, decisions.baseAddress, UInt64(decisions.count),
                        cs_osdi_precision_swift_task_cancelled, nil, &native, &issue)
                }}
            }}
        }
        let status: Int32
        if let tape = retainedTape { status = tape.storage.withUnsafeBufferPointer { invoke($0) } }
        else { status = invoke(UnsafeBufferPointer(start: nil, count: 0)) }
        guard status == 0 else {
            let primary = OSDIPrecisionChartError.native(OSDIPrecisionNativeBridge.issue(issue))
            do { try close() }
            catch { throw .cleanup(primary: primary, secondary: error) }
            throw primary
        }
        let formulas = native.formulas, nodes = program.topology.nodes.count
        let (matrix, overflow) = nodes.multipliedReportingOverflow(by: nodes)
        guard !overflow, formulas.raw_node_count == nodes, formulas.program_has_time_input <= 1,
              native.semantics == mode.rawValue, native.guard_count <= chartLimits.maximumGuardRecords,
              native.guard_count <= UInt64(Int.max), native.completed_steps <= chartLimits.maximumSteps,
              native.guard_count == 0 || native.guards != nil,
              nodes == 0 || (formulas.f_lower != nil && formulas.f_upper != nil && formulas.q_lower != nil && formulas.q_upper != nil),
              matrix == 0 || (formulas.g_lower != nil && formulas.g_upper != nil && formulas.c_lower != nil && formulas.c_upper != nil) else {
            do { try close() }
            catch { throw .cleanup(primary: .runtime(.identityMismatch), secondary: error) }
            throw .runtime(.identityMismatch)
        }
        // The native boundary validates the entire extended geometry, all fixed
        // records and ordered planes before this exclusive synchronous borrow.
        let guards = OSDIPrecisionChartGuardCollectionView(storage: native.guard_count == 0 ? Span() :
            Span(_unsafeStart: native.guards!, count: Int(native.guard_count)))
        return body(OSDIPrecisionChartEvaluationView(
            fLower: nodes == 0 ? Span() : Span(_unsafeStart: formulas.f_lower!, count: nodes),
            fUpper: nodes == 0 ? Span() : Span(_unsafeStart: formulas.f_upper!, count: nodes),
            qLower: nodes == 0 ? Span() : Span(_unsafeStart: formulas.q_lower!, count: nodes),
            qUpper: nodes == 0 ? Span() : Span(_unsafeStart: formulas.q_upper!, count: nodes),
            gLower: matrix == 0 ? Span() : Span(_unsafeStart: formulas.g_lower!, count: matrix),
            gUpper: matrix == 0 ? Span() : Span(_unsafeStart: formulas.g_upper!, count: matrix),
            cLower: matrix == 0 ? Span() : Span(_unsafeStart: formulas.c_lower!, count: matrix),
            cUpper: matrix == 0 ? Span() : Span(_unsafeStart: formulas.c_upper!, count: matrix),
            guards: guards, binding: chartBinding, semantics: mode,
            maximumTimestep: formulas.has_maximum_timestep == 0 ? nil : formulas.maximum_timestep,
            usedPrecisionBits: formulas.used_precision_bits, completedWorkUnits: formulas.completed_work_units,
            completedSteps: native.completed_steps, requestSequence: formulas.request_sequence,
            programHasTimeInput: formulas.program_has_time_input != 0))
    }
}
