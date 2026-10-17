/// Scoped shared-slab views. G/C are inexact Newton aids, never residual certificates.
public struct OSDIPrecisionEvaluationView: ~Escapable {
    public let fHigh: Span<Double>
    public let fLow: Span<Double>
    public let fRadius: Span<Double>
    public let deltaQHigh: Span<Double>
    public let deltaQLow: Span<Double>
    public let deltaQRadius: Span<Double>
    public let g: Span<Double>
    public let c: Span<Double>
    public let maximumTimestep: Double?
    public let usedPrecisionBits: UInt32
    public let completedWorkUnits: UInt64
    public let requestSequence: UInt64
    @_lifetime(copy fHigh, copy fLow, copy fRadius, copy deltaQHigh, copy deltaQLow, copy deltaQRadius, copy g, copy c)
    internal init(fHigh: Span<Double>, fLow: Span<Double>, fRadius: Span<Double>,
                  deltaQHigh: Span<Double>, deltaQLow: Span<Double>, deltaQRadius: Span<Double>,
                  g: Span<Double>, c: Span<Double>, maximumTimestep: Double?, usedPrecisionBits: UInt32,
                  completedWorkUnits: UInt64, requestSequence: UInt64) {
        self.fHigh = fHigh; self.fLow = fLow; self.fRadius = fRadius
        self.deltaQHigh = deltaQHigh; self.deltaQLow = deltaQLow; self.deltaQRadius = deltaQRadius
        self.g = g; self.c = c; self.maximumTimestep = maximumTimestep
        self.usedPrecisionBits = usedPrecisionBits; self.completedWorkUnits = completedWorkUnits; self.requestSequence = requestSequence
    }
}
