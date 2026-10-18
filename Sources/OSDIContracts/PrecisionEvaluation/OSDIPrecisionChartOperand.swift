/// Source register identity and its outward operand facts; absent operands have no register.
public struct OSDIPrecisionChartOperand: Sendable, Equatable {
    public enum Kind: UInt32, Sendable { case absent = 0, real = 1, integer = 2, boolean = 3, string = 4
        case negativePrimitiveInfinity = 5, positivePrimitiveInfinity = 6
    }
    public let register: UInt32?
    public let kind: Kind
    /// Integer/Boolean bits, or an exact constant index for a tagged infinity.
    public let integerBits: UInt32
    public var sourcePrimitiveConstantIndex: UInt32? {
        kind == .negativePrimitiveInfinity || kind == .positivePrimitiveInfinity ? integerBits : nil
    }
    public let lower: Double
    public let upper: Double
    public init(register: UInt32?, kind: Kind, integerBits: UInt32, lower: Double, upper: Double) {
        self.register = register; self.kind = kind; self.integerBits = integerBits
        self.lower = lower; self.upper = upper
    }
}
