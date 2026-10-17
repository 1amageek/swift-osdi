import COSDI

internal enum NativeIssueBridge {
    static func issue(_ error: cs_osdi_error) -> OSDINativeIssue {
        OSDINativeIssue(code: error.code, descriptorIndex: error.descriptor_index,
            field: text(error.field), detail: text(error.detail), modelFlags: error.model_flags,
            setupErrorCount: error.setup_error_count, setupErrorCode: error.setup_error_code,
            setupParameterID: error.setup_parameter_id)
    }

    static func cleanupIssue(_ error: cs_osdi_error) -> OSDINativeIssue {
        OSDINativeIssue(code: 9, descriptorIndex: error.descriptor_index,
            field: "close", detail: text(error.cleanup_detail), modelFlags: error.model_flags,
            setupErrorCount: error.setup_error_count, setupErrorCode: error.setup_error_code,
            setupParameterID: error.setup_parameter_id)
    }

    private static func text<Value>(_ value: Value) -> String {
        withUnsafeBytes(of: value) { bytes in
            let end = bytes.firstIndex(of: 0) ?? bytes.endIndex
            return String(decoding: bytes[..<end], as: UTF8.self)
        }
    }
}
