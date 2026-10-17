import CircuiteFoundation
import OSDIContracts

/// Runtime failures retain both primary causes and retryable cleanup ownership.
public enum OSDIError: Error, Sendable {
    case invalidRange
    case cancelled
    case unsupportedDigest
    case resourceLimit
    case allocationFailed(byteCount: UInt64)
    case closed
    case libraryInUse
    case identityMismatch
    case invalidDescriptorName(index: Int)
    case contract(OSDIContractError)
    case artifact(ArtifactAccessError)
    case artifactClose(ArtifactAccessTerminationError)
    case native(OSDINativeIssue)
    case precision(OSDIPrecisionIssue)
    indirect case precisionCleanup(cause: Self, owner: OSDIPendingPrecisionCleanup)
    case io(operation: String, code: Int32)
    case host(operation: String, underlying: any Error)
    indirect case cleanupRequired(cause: Self, owner: OSDIPendingLibraryCleanup)
    indirect case cleanup(primary: Self, secondary: Self)
    indirect case pendingCleanup(primary: Self, secondary: Self, owner: OSDIPendingLibraryCleanup)
}
