/// A bounded native model diagnostic, retaining the original OSDI level bits.
public struct OSDIDiagnostic: Sendable, Equatable {
    public let level: UInt32
    public let message: String

    public init(level: UInt32, message: String) {
        self.level = level
        self.message = message
    }
}
