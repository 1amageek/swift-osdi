import COSDI
import OSDIContracts

internal enum OSDIPrecisionNativeBridge {
    static func issue(_ native: cs_osdi_precision_process_issue) -> OSDIPrecisionIssue {
        OSDIPrecisionIssue(code: OSDIPrecisionIssue.Code(rawValue: native.code) ?? .protocolViolation,
            workerStatus: native.worker_status, instruction: native.instruction == UInt32.max ? nil : native.instruction,
            exitCode: native.exit_code < 0 ? nil : native.exit_code, signal: native.signal_number == 0 ? nil : native.signal_number,
            systemErrno: native.system_errno, requestSequence: native.request_sequence)
    }
}

@c @implementation
func cs_osdi_precision_swift_task_cancelled(_ context: UnsafeMutableRawPointer?) -> Int32 {
    Task.isCancelled ? 1 : 0
}
