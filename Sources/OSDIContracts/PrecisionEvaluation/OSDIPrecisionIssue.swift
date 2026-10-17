/// A failed precision operation publishes no numerical view.
public struct OSDIPrecisionIssue: Error, Sendable, Equatable {
    public enum Code: Int32, Sendable {
        case invalidInput = 1, resourceLimit, timeout, cancelled, workerFailure, protocolViolation
        case numericFailure, cleanupFailure, closed, io, unsupported
    }
    public let code: Code
    public let workerStatus: UInt32
    public let instruction: UInt32?
    public let exitCode: Int32?
    public let signal: Int32?
    public let systemErrno: Int32
    public let requestSequence: UInt64
    public init(code: Code, workerStatus: UInt32, instruction: UInt32?, exitCode: Int32?, signal: Int32?,
                systemErrno: Int32, requestSequence: UInt64) {
        self.code = code; self.workerStatus = workerStatus; self.instruction = instruction
        self.exitCode = exitCode; self.signal = signal; self.systemErrno = systemErrno; self.requestSequence = requestSequence
    }
}
