import CircuiteFoundation
import OSDIContracts

public protocol OSDIPrecisionDecisionCoverSessionPreparing: Sendable {
    func prepareDecisionCover(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        limits: OSDIPrecisionDecisionCoverLimits) async throws(OSDIPrecisionDecisionCoverError) -> OSDIPrecisionSession
}
