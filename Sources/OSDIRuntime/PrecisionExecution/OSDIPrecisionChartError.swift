import OSDIContracts

/// Chart failures preserve the complete lower failure and any retained cleanup authority.
public enum OSDIPrecisionChartError: Error, Sendable {
    case runtime(OSDIError)
    case tapeMismatch(OSDIPrecisionIssue)
    case guardCapacityExceeded(OSDIPrecisionIssue)
    case stepLimitExceeded(OSDIPrecisionIssue)
    indirect case cleanup(primary: Self, secondary: OSDIError)
    internal static func native(_ issue: OSDIPrecisionIssue) -> Self {
        switch issue.workerStatus {
        case 100: .tapeMismatch(issue)
        case 101: .guardCapacityExceeded(issue)
        case 102: .stepLimitExceeded(issue)
        default: .runtime(.precision(issue))
        }
    }
}
