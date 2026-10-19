import OSDIContracts

public enum OSDIPrecisionConstitutiveError: Error, Sendable {
    case runtime(OSDIError)
    case cover(OSDIPrecisionDecisionCoverError)
    case capacityExceeded(OSDIPrecisionIssue)
    case coefficientLimitExceeded(OSDIPrecisionIssue)
    case invalidIdentity(OSDIPrecisionIssue)
    case emptyCoordinateCondition(OSDIPrecisionIssue)
    indirect case cleanup(primary: Self, secondary: OSDIError)
    internal static func native(_ issue: OSDIPrecisionIssue) -> Self {
        switch issue.workerStatus {
        case 120, 123: .invalidIdentity(issue)
        case 121: .capacityExceeded(issue)
        case 122: .coefficientLimitExceeded(issue)
        case 124: .emptyCoordinateCondition(issue)
        case 102, 112...116: .cover(.native(issue))
        default: .runtime(.precision(issue))
        }
    }
}
