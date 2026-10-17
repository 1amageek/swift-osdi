import Foundation

struct OSDIInspectRequest: Decodable {
    struct Limits: Decodable {
        let maximumDescriptors: UInt32
        let maximumMetadataBytes: UInt64
        let maximumStorageBytes: UInt64
    }
    struct ReadBudget: Decodable {
        let maximumPageByteCount: UInt64
        let maximumTotalByteCount: UInt64
        let maximumPageCount: UInt64
        let maximumWorkUnitCount: UInt64
        let maximumDurationNanoseconds: UInt64
    }
    let schemaVersion: Int
    let libraryPath: String
    let expectedSHA256: String
    let expectedByteCount: UInt64
    let limits: Limits
    let readBudget: ReadBudget
    let verifyStorage: Bool
}
