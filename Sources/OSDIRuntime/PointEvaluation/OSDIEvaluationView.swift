import COSDI
import OSDIContracts

/// Scoped native buffers; F/Q use raw nodes and G/C are raw-node row-major matrices.
public struct OSDIEvaluationView: ~Escapable {
    public let f: Span<Double>
    public let q: Span<Double>
    public let g: Span<Double>
    public let c: Span<Double>
    public let nextState: Span<Double>
    public let modelFlags: UInt32
    public let diagnosticCount: Int
    private let nativeDiagnostics: UnsafePointer<cs_osdi_diagnostic>?
    private let maximumDiagnosticBytes: UInt64

    @_lifetime(copy f, copy q, copy g, copy c, copy nextState)
    internal init(f: Span<Double>, q: Span<Double>, g: Span<Double>, c: Span<Double>,
                  nextState: Span<Double>, modelFlags: UInt32, diagnosticCount: Int,
                  nativeDiagnostics: UnsafePointer<cs_osdi_diagnostic>?, maximumDiagnosticBytes: UInt64) {
        self.f = f; self.q = q; self.g = g; self.c = c; self.nextState = nextState
        self.modelFlags = modelFlags; self.diagnosticCount = diagnosticCount
        self.nativeDiagnostics = nativeDiagnostics; self.maximumDiagnosticBytes = maximumDiagnosticBytes
    }

    /// Copies one diagnostic only when the consumer requests it.
    public func diagnostic(at index: Int) throws(OSDIError) -> OSDIDiagnostic {
        guard index >= 0, index < diagnosticCount, let nativeDiagnostics else { throw .invalidRange }
        let diagnostic = nativeDiagnostics[index]
        return OSDIDiagnostic(level: diagnostic.level,
            message: try OSDINativeText.copy(diagnostic.message, maximumBytes: maximumDiagnosticBytes))
    }
}
