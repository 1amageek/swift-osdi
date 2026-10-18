import OSDIContracts
import Testing

@Suite(.timeLimit(.minutes(1)))
struct OSDIPrecisionDecisionCoverValueTests {
    @Test func canonicalInlineIntegerAndPolicyBoundsRefuseMalformedInputs() throws {
        let words: [UInt64] = [UInt64.max, 1]
        let value = try OSDIPrecisionCellInteger(isNegative: true, magnitude: words.span)
        #expect(value.isNegative && value.wordCount == 2 && value[0] == UInt64.max && value[1] == 1)
        #expect(value == (try OSDIPrecisionCellInteger(isNegative: true, magnitude: words.span)))
        let empty: [UInt64] = [], zero = [UInt64(0)]
        #expect(try OSDIPrecisionCellInteger(isNegative: false, magnitude: empty.span) == .zero)
        #expect(throws: OSDIContractError.invalidLimits) { try OSDIPrecisionCellInteger(isNegative: true, magnitude: empty.span) }
        #expect(throws: OSDIContractError.invalidLimits) { try OSDIPrecisionCellInteger(isNegative: false, magnitude: zero.span) }
        var maximum = [UInt64](repeating: 0, count: 17); maximum[16] = 1
        #expect(try OSDIPrecisionCellInteger(isNegative: false, magnitude: maximum.span).wordCount == 17)
        maximum[16] = 2
        #expect(throws: OSDIContractError.invalidLimits) { try OSDIPrecisionCellInteger(isNegative: false, magnitude: maximum.span) }
        #expect(throws: OSDIContractError.invalidLimits) {
            try OSDIPrecisionDecisionCoverLimits(maximumSteps: 1, maximumGuardRecords: 1, maximumCopiedBytes: 1,
                maximumCells: 1, maximumNodes: UInt32.max, maximumLeaves: 1, maximumPending: 1, maximumDepth: 1, maximumIntegerBits: 1025)
        }
        #expect(throws: OSDIContractError.invalidLimits) {
            try OSDIPrecisionDecisionCoverLimits(maximumSteps: 1, maximumGuardRecords: 1, maximumCopiedBytes: 1,
                maximumCells: 1, maximumNodes: 1, maximumLeaves: 1, maximumPending: 1, maximumDepth: 1, maximumIntegerBits: 1026)
        }
    }
}
