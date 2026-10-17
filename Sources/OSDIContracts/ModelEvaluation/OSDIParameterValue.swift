public enum OSDIParameterValue: Sendable, Equatable {
    case real(Double)
    case integer(Int32)
    case string(String)

    public var type: OSDIParameterType {
        switch self {
        case .real: .real
        case .integer: .integer
        case .string: .string
        }
    }

    internal var isValid: Bool {
        switch self {
        case .real(let value): value.isFinite
        case .integer: true
        case .string(let value): !value.utf8.contains(0)
        }
    }
}
