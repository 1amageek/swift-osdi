import OSDIContracts

/// Complete-cover failures preserve the complete primary and retained cleanup causes.
public enum OSDIPrecisionDecisionCoverError: Error, Sendable {
    case runtime(OSDIError)
    case capacityExceeded(OSDIPrecisionIssue)
    case stepLimitExceeded(OSDIPrecisionIssue)
    case cellPrecisionExceeded(OSDIPrecisionIssue)
    case invalidCover(OSDIPrecisionIssue)
    indirect case cleanup(primary: Self, secondary: OSDIError)
    internal static func native(_ issue: OSDIPrecisionIssue) -> Self {
        switch issue.workerStatus {
        case 102: .stepLimitExceeded(issue)
        case 113, 116: .capacityExceeded(issue)
        case 114: .cellPrecisionExceeded(issue)
        case 112, 115: .invalidCover(issue)
        default: .runtime(.precision(issue))
        }
    }
}
