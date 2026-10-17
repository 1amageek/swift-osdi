/// Explicit native phase, absolute seconds and optional frequency in hertz.
public struct OSDIEvaluationContext: Sendable, Equatable {
    public let mode: OSDIEvaluationMode
    public let timeSeconds: Double
    public let noiseFrequencyHz: Double?

    public static let dc = Self()

    private init() { mode = .dc; timeSeconds = 0; noiseFrequencyHz = nil }

    public init(mode: OSDIEvaluationMode, timeSeconds: Double,
                noiseFrequencyHz: Double? = nil) throws(OSDIContractError) {
        guard timeSeconds.isFinite, timeSeconds >= 0,
              mode == .transient || timeSeconds == 0 else { throw .invalidEvaluationContext }
        if mode == .noise {
            guard let noiseFrequencyHz, noiseFrequencyHz.isFinite, noiseFrequencyHz > 0 else {
                throw .invalidEvaluationContext
            }
        } else if noiseFrequencyHz != nil { throw .invalidEvaluationContext }
        self.mode = mode; self.timeSeconds = timeSeconds; self.noiseFrequencyHz = noiseFrequencyHz
    }
}
