/// Copied native diagnostics contain no borrowed pointers into a loaded image.
public struct OSDINativeIssue: Sendable, Equatable {
    public let code: Int32
    public let descriptorIndex: UInt32
    public let field: String
    public let detail: String
    public let modelFlags: UInt32
    public let setupErrorCount: UInt32
    public let setupErrorCode: UInt32
    public let setupParameterID: UInt32

    public init(code: Int32, descriptorIndex: UInt32, field: String, detail: String,
                modelFlags: UInt32 = 0, setupErrorCount: UInt32 = 0,
                setupErrorCode: UInt32 = 0, setupParameterID: UInt32 = 0) {
        self.code = code
        self.descriptorIndex = descriptorIndex
        self.field = field
        self.detail = detail
        self.modelFlags = modelFlags
        self.setupErrorCount = setupErrorCount
        self.setupErrorCode = setupErrorCode
        self.setupParameterID = setupParameterID
    }
}
