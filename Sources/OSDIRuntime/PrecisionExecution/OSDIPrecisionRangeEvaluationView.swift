/// Scoped absolute F/Q and spatial G/C bounds over a closed smooth box.
public struct OSDIPrecisionRangeEvaluationView: ~Escapable {
    public let fLower: Span<Double>
    public let fUpper: Span<Double>
    public let qLower: Span<Double>
    public let qUpper: Span<Double>
    public let gLower: Span<Double>
    public let gUpper: Span<Double>
    public let cLower: Span<Double>
    public let cUpper: Span<Double>
    public let maximumTimestep: Double?
    public let usedPrecisionBits: UInt32
    public let completedWorkUnits: UInt64
    public let requestSequence: UInt64
    public let programHasTimeInput: Bool
    @_lifetime(copy fLower, copy fUpper, copy qLower, copy qUpper, copy gLower, copy gUpper, copy cLower, copy cUpper)
    internal init(fLower: Span<Double>, fUpper: Span<Double>, qLower: Span<Double>, qUpper: Span<Double>,
                  gLower: Span<Double>, gUpper: Span<Double>, cLower: Span<Double>, cUpper: Span<Double>,
                  maximumTimestep: Double?, usedPrecisionBits: UInt32, completedWorkUnits: UInt64,
                  requestSequence: UInt64, programHasTimeInput: Bool) {
        self.fLower = fLower; self.fUpper = fUpper; self.qLower = qLower; self.qUpper = qUpper
        self.gLower = gLower; self.gUpper = gUpper; self.cLower = cLower; self.cUpper = cUpper
        self.maximumTimestep = maximumTimestep; self.usedPrecisionBits = usedPrecisionBits
        self.completedWorkUnits = completedWorkUnits; self.requestSequence = requestSequence
        self.programHasTimeInput = programHasTimeInput
    }
}
