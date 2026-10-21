import OSDIContracts
import OSDIRuntime

struct OSDIInspectReport: Encodable {
    struct Descriptor: Encodable {
        let index: Int
        let name: String
        let nodeCount: Int
        let terminalCount: Int
        let jacobianEntryCount: Int
        let stateCount: Int
        let instanceByteCount: Int
        let modelByteCount: Int
        init(_ info: OSDIDescriptorInfo) {
            index = info.index; name = info.name; nodeCount = info.nodeCount
            terminalCount = info.terminalCount; jacobianEntryCount = info.jacobianEntryCount
            stateCount = info.stateCount; instanceByteCount = info.instanceByteCount
            modelByteCount = info.modelByteCount
        }
    }
    let schemaVersion = 1
    let succeeded: Bool
    let stage = "osdi-admission"
    let expectedSHA256: String?
    let expectedByteCount: UInt64?
    let descriptorStride: UInt64?
    let descriptors: [Descriptor]?
    let storageVerified: Bool?
    let reason: String?
    let detail: String?
    let nativeStatus: Int32?
}
