import COSDI
import OSDIContracts
import Synchronization

/// Exclusive transferable native point session. Configuration and topology are immutable.
public struct OSDIPreparedModel: ~Copyable, Sendable {
    internal struct State { var handle: OpaquePointer? }
    private let state: Mutex<State>
    private var lease: OSDIStorageLease?
    private let limits: OSDIEvaluationLimits
    private let descriptorIndex: UInt32
    public let topology: OSDIPreparedTopology
    public let noiseSources: [OSDINoiseSourceInfo]
    public let parameterMetadata: [OSDIParameterInfo]
    public let resolvedParameters: [OSDIResolvedParameter]
    public let diagnostics: [OSDIDiagnostic]

    internal init(handle: sending OpaquePointer, lease: consuming OSDIStorageLease,
                  topology: OSDIPreparedTopology, noiseSources: [OSDINoiseSourceInfo], parameterMetadata: [OSDIParameterInfo],
                  resolvedParameters: [OSDIResolvedParameter], diagnostics: [OSDIDiagnostic],
                  limits: OSDIEvaluationLimits) {
        state = Mutex(State(handle: handle))
        descriptorIndex = UInt32(lease.descriptor.index)
        self.lease = consume lease
        self.topology = topology; self.noiseSources = noiseSources; self.parameterMetadata = parameterMetadata
        self.resolvedParameters = resolvedParameters; self.diagnostics = diagnostics; self.limits = limits
    }

    /// Frees native buffers before releasing the image lease. Repeated calls are harmless.
    public mutating func close() {
        let handle = state.withLock { $0.handle.take() }
        if let handle { cs_osdi_evaluator_destroy(handle) }
        lease = nil
    }

    deinit {
        let handle = state.withLock { $0.handle.take() }
        if let handle { cs_osdi_evaluator_destroy(handle) }
    }

    internal mutating func evaluate<Result>(context: OSDIEvaluationContext, unknowns: borrowing Span<Double>, previousState: borrowing Span<Double>,
                                            _ body: (OSDIEvaluationView) -> Result) throws(OSDIError) -> Result {
        guard let handle = state.withLock({ $0.handle }) else { throw .closed }
        guard unknowns.count == topology.activeUnknownCount, previousState.count == topology.stateCount else { throw .invalidRange }
        var error = cs_osdi_error()
        var nativeContext = cs_osdi_evaluation_context(mode: context.mode.rawValue, time_seconds: context.timeSeconds,
            noise_frequency_hz: context.noiseFrequencyHz ?? 0)
        let (status, result) = unknowns.withUnsafeBufferPointer { values in
            previousState.withUnsafeBufferPointer { previous in
                var result = cs_osdi_result_view()
                let status = cs_osdi_evaluate_context(handle, &nativeContext, values.baseAddress, UInt64(values.count), previous.baseAddress,
                    UInt64(previous.count), &result, &error)
                return (status, result)
            }
        }
        guard status == 0 else { throw .native(NativeIssueBridge.issue(error)) }
        guard result.model_flags == 0 else {
            throw .native(OSDINativeIssue(code: 17, descriptorIndex: descriptorIndex,
                field: "model_flags", detail: "Native model returned nonzero evaluation flags", modelFlags: result.model_flags))
        }
        let nodes = topology.nodes.count, states = topology.stateCount
        let (matrixCount, overflow) = nodes.multipliedReportingOverflow(by: nodes)
        guard !overflow, Int(result.node_count) == nodes,
              Int(result.active_unknown_count) == topology.activeUnknownCount, Int(result.state_count) == states,
              nodes == 0 || (result.f != nil && result.q != nil && result.g != nil && result.c != nil),
              states == 0 || result.next_state != nil,
              result.noise_count == (context.mode == .noise ? noiseSources.count : 0),
              result.noise_count == 0 || result.noise_densities != nil,
              result.has_maximum_timestep <= 1,
              result.has_maximum_timestep == 0 || (result.maximum_timestep.isFinite && result.maximum_timestep > 0),
              result.diagnostic_count <= limits.maximumDiagnosticCount,
              result.diagnostic_count == 0 || result.diagnostics != nil else { throw .invalidRange }
        let f = nodes == 0 ? Span<Double>() : Span(_unsafeStart: result.f!, count: nodes)
        let q = nodes == 0 ? Span<Double>() : Span(_unsafeStart: result.q!, count: nodes)
        let g = matrixCount == 0 ? Span<Double>() : Span(_unsafeStart: result.g!, count: matrixCount)
        let c = matrixCount == 0 ? Span<Double>() : Span(_unsafeStart: result.c!, count: matrixCount)
        let next = states == 0 ? Span<Double>() : Span(_unsafeStart: result.next_state!, count: states)
        let noise = result.noise_count == 0 ? Span<Double>() : Span(_unsafeStart: result.noise_densities!, count: Int(result.noise_count))
        return body(OSDIEvaluationView(f: f, q: q, g: g, c: c, nextState: next, noiseDensities: noise,
            maximumTimestep: result.has_maximum_timestep == 0 ? nil : result.maximum_timestep, modelFlags: result.model_flags,
            diagnosticCount: Int(result.diagnostic_count), nativeDiagnostics: result.diagnostics,
            maximumDiagnosticBytes: limits.maximumDiagnosticBytes))
    }
}
