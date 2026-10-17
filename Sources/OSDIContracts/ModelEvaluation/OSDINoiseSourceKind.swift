/// Native independent source spectrum classification; densities come from load_noise.
public enum OSDINoiseSourceKind: UInt32, Sendable, Equatable {
    case white = 0
    case flicker = 1
    case table = 2
}
