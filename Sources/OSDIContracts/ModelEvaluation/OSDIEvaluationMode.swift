/// Analysis phase, independent of the caller's solver and accepted-history policy.
public enum OSDIEvaluationMode: UInt32, Sendable, Equatable {
    case dc = 0
    case ac = 1
    case transient = 2
    case transientInitialization = 3
    case noise = 4
}
