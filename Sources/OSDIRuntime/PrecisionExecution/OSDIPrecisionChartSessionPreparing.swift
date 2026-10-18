import CircuiteFoundation
import OSDIContracts

public protocol OSDIPrecisionChartSessionPreparing: Sendable {
    func prepareChart(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        limits: OSDIPrecisionChartLimits) async throws(OSDIPrecisionChartError) -> OSDIPrecisionSession
}
