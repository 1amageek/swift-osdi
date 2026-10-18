import CircuiteFoundation
import OSDIContracts

public protocol OSDIPrecisionRangeSessionPreparing: Sendable {
    func prepareRange(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions) async throws(OSDIError) -> OSDIPrecisionSession
}
