import CircuiteFoundation
import OSDIContracts

public protocol OSDIPrecisionConstitutiveSessionPreparing: Sendable {
    func prepareConstitutiveIdentities(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent,
        source: any ArtifactAccessing, numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        coverLimits: OSDIPrecisionDecisionCoverLimits, limits: OSDIPrecisionConstitutiveLimits,
        coordinateQuotient: OSDIPrecisionCoordinateQuotient)
        async throws(OSDIPrecisionConstitutiveError) -> OSDIPrecisionSession
}
