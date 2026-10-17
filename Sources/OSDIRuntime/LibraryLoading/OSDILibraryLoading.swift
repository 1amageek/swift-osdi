import CircuiteFoundation
import OSDIContracts

public protocol OSDILibraryLoading: Sendable {
    func load(_ intent: ArtifactAccessIntent, source: any ArtifactAccessing,
              limits: OSDILoadLimits) async throws(OSDIError) -> OSDILibrary
}
