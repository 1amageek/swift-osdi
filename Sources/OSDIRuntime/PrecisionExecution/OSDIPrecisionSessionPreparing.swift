import CircuiteFoundation
import OSDIContracts

public protocol OSDIPrecisionSessionPreparing: Sendable {
    func snapshot(library: borrowing OSDILibrary, descriptorIndex: Int, model: borrowing OSDIPreparedModel,
        preparation: OSDIPreparationOptions, semanticProfile: ContentDigest,
        options: OSDIPrecisionOptions) throws(OSDIError) -> OSDIPrecisionProgram
    /// Explicit ASCII construction profile with mandatory shared caller accounting.
    func snapshot(library: borrowing OSDILibrary, descriptorIndex: Int, model: borrowing OSDIPreparedModel,
        preparation: OSDIPreparationOptions, semanticProfile: ContentDigest,
        options: OSDIPrecisionOptions, control: any OSDIPrecisionSnapshotControlling) throws(OSDIError) -> OSDIPrecisionProgram
    func prepare(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions) async throws(OSDIError) -> OSDIPrecisionSession
}

public extension OSDIPrecisionSessionPreparing {
    func snapshot(library: borrowing OSDILibrary, descriptorIndex: Int, model: borrowing OSDIPreparedModel,
        preparation: OSDIPreparationOptions, semanticProfile: ContentDigest,
        options: OSDIPrecisionOptions, control: any OSDIPrecisionSnapshotControlling) throws(OSDIError) -> OSDIPrecisionProgram {
        throw .contract(.unsupportedEvaluationContext)
    }
}
