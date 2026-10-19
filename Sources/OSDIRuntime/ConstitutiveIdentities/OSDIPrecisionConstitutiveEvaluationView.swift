import COSDI
import OSDIContracts

/// Checked common source-linear kernel; no rank, root, nonemptiness or flow authority.
public struct OSDIPrecisionConstitutiveEvaluationView: ~Escapable {
    public let binding: OSDIPrecisionConstitutiveBinding
    public let cover: OSDIPrecisionDecisionCoverEvaluationView
    public var claimCount: Int { claims.count }
    private let claims: Span<cs_osdi_identity_claim_v5>
    private let coefficients: Span<cs_osdi_identity_coefficient_v5>
    private let words: Span<UInt64>
    private let rawRows: Int
    @_lifetime(copy cover, copy claims, copy coefficients, copy words)
    internal init(cover: OSDIPrecisionDecisionCoverEvaluationView, binding: OSDIPrecisionConstitutiveBinding, claims: Span<cs_osdi_identity_claim_v5>,
        coefficients: Span<cs_osdi_identity_coefficient_v5>, words: Span<UInt64>, rawRows: Int) {
        self.binding = binding; self.cover = cover; self.claims = claims; self.coefficients = coefficients; self.words = words; self.rawRows = rawRows
    }
    public func withClaim<Result>(at index: Int, _ body: (OSDIPrecisionConstitutiveClaimView) -> Result) -> Result {
        precondition(index >= 0 && index < claimCount)
        return body(OSDIPrecisionConstitutiveClaimView(binding: binding, kind: OSDIPrecisionConstitutiveKind(rawValue: claims[index].kind)!,
            coefficients: coefficients.extracting(index*rawRows..<(index+1)*rawRows), words: words))
    }
}
