/// Canonical exact signed integer. Words are little-endian and stored inline.
public struct OSDIPrecisionCellInteger: Sendable, Equatable {
    public let isNegative: Bool
    public let wordCount: Int
    private let words: InlineArray<17, UInt64>
    public static let zero = Self()
    private init() { isNegative = false; wordCount = 0; words = .init(repeating: 0) }
    public init(isNegative: Bool, magnitude: borrowing Span<UInt64>) throws(OSDIContractError) {
        guard magnitude.count <= 17, !magnitude.isEmpty || !isNegative else { throw .invalidLimits }
        if !magnitude.isEmpty {
            let high = magnitude[magnitude.count - 1]
            guard high != 0, 64 * (magnitude.count - 1) + 64 - high.leadingZeroBitCount <= 1025 else { throw .invalidLimits }
        }
        var words: InlineArray<17, UInt64> = .init(repeating: 0)
        for i in 0..<magnitude.count { words[i] = magnitude[i] }
        self.isNegative = isNegative; self.wordCount = magnitude.count; self.words = words
    }
    public subscript(index: Int) -> UInt64 {
        precondition(index >= 0 && index < wordCount)
        return words[index]
    }
    public static func == (lhs: Self, rhs: Self) -> Bool {
        guard lhs.isNegative == rhs.isNegative, lhs.wordCount == rhs.wordCount else { return false }
        for i in 0..<lhs.wordCount where lhs.words[i] != rhs.words[i] { return false }
        return true
    }
}
