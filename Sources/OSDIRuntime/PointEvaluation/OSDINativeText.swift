import COSDI

internal enum OSDINativeText {
    static func copy(_ value: cs_osdi_string_view, maximumBytes: UInt64) throws(OSDIError) -> String {
        guard value.length <= maximumBytes, let count = Int(exactly: value.length) else { throw .resourceLimit }
        if count == 0 { return "" }
        guard let data = value.data else { throw .invalidRange }
        let bytes = UnsafeRawBufferPointer(start: data, count: count)
        guard let text = String(validating: bytes, as: UTF8.self), !text.utf8.contains(0) else { throw .invalidRange }
        return text
    }
}
