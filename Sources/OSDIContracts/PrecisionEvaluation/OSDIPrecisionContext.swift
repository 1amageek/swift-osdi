/// Explicit endpoint context for the stateless precision profile.
public struct OSDIPrecisionContext: Sendable, Equatable {
    public enum Mode: UInt32, Sendable { case transient = 2, transientInitialization = 3 }
    public let mode: Mode
    public let timeHigh: Double
    public let timeLow: Double
    public init(mode: Mode, timeHigh: Double, timeLow: Double) throws(OSDIContractError) {
        guard timeHigh.isFinite, timeLow.isFinite, timeHigh + timeLow == timeHigh,
              timeHigh >= 0, !(timeHigh == 0 && timeLow < 0),
              mode != .transientInitialization || (timeHigh == 0 && timeLow == 0) else { throw .invalidEvaluationContext }
        self.mode = mode; self.timeHigh = timeHigh; self.timeLow = timeLow
    }
}
