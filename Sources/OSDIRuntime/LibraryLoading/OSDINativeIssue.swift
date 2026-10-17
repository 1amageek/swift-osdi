/// Copied native diagnostics contain no borrowed pointers into a loaded image.
public struct OSDINativeIssue: Sendable, Equatable {
    public let code: Int32
    public let descriptorIndex: UInt32
    public let field: String
    public let detail: String

    public init(code: Int32, descriptorIndex: UInt32, field: String, detail: String) {
        self.code = code
        self.descriptorIndex = descriptorIndex
        self.field = field
        self.detail = detail
    }
}
