/// Exact signed numerator / 2^denominatorPower. Words are least-significant first.
public struct OSDIPrecisionDyadicCoefficientView: ~Escapable {
    public let negative: Bool
    public let denominatorPower: UInt64
    public let numeratorWords: Span<UInt64>
    @_lifetime(copy numeratorWords)
    internal init(negative: Bool, denominatorPower: UInt64, numeratorWords: Span<UInt64>) {
        self.negative = negative; self.denominatorPower = denominatorPower; self.numeratorWords = numeratorWords
    }
}
