import COSDI
import OSDIContracts

/// One exact identity over every original raw output row.
public struct OSDIPrecisionConstitutiveClaimView: ~Escapable {
    public let binding: OSDIPrecisionConstitutiveBinding
    public let kind: OSDIPrecisionConstitutiveKind
    public var rawRowCount: Int { coefficients.count }
    private let coefficients: Span<cs_osdi_identity_coefficient_v5>
    private let words: Span<UInt64>
    @_lifetime(copy coefficients, copy words)
    internal init(binding: OSDIPrecisionConstitutiveBinding, kind: OSDIPrecisionConstitutiveKind, coefficients: Span<cs_osdi_identity_coefficient_v5>, words: Span<UInt64>) {
        self.binding = binding; self.kind = kind; self.coefficients = coefficients; self.words = words
    }
    public func withCoefficient<Result>(at rawRow: Int, _ body: (OSDIPrecisionDyadicCoefficientView) -> Result) -> Result {
        precondition(rawRow >= 0 && rawRow < rawRowCount)
        let value = coefficients[rawRow]
        return body(OSDIPrecisionDyadicCoefficientView(negative: value.negative != 0, denominatorPower: value.denominator_power,
            numeratorWords: words.extracting(Int(value.first_word)..<Int(value.first_word + value.word_count))))
    }
}
