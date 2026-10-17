import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts

/// Cold exact artifact acquisition and setup serialization; solver state remains caller-owned.
public struct OSDINativePrecisionSessionPreparer: OSDIPrecisionSessionPreparing {
    public let stagingParentDirectory: URL
    public init(stagingParentDirectory: URL = FileManager.default.temporaryDirectory) {
        self.stagingParentDirectory = stagingParentDirectory
    }
    public func snapshot(library: borrowing OSDILibrary, descriptorIndex: Int, model: borrowing OSDIPreparedModel,
        preparation: OSDIPreparationOptions, semanticProfile: ContentDigest,
        options: OSDIPrecisionOptions) throws(OSDIError) -> OSDIPrecisionProgram {
        guard model.precisionPreparation == preparation, model.precisionModelReference == library.reference,
              Int(model.descriptorIndex) == descriptorIndex else { throw .identityMismatch }
        return try OSDIPrecisionProgramBuilder.build(library: library, descriptorIndex: descriptorIndex,
            topology: model.topology, metadata: model.parameterMetadata, preparation: preparation,
            semanticProfile: semanticProfile, options: options)
    }
    public func prepare(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions) async throws(OSDIError) -> OSDIPrecisionSession {
        guard UInt64(program.programBytes.count) <= options.maximumProgramBytes,
              UInt64(program.setupBytes.count) <= options.maximumMappingBytes else { throw .resourceLimit }
        let programSHA = try OSDIPrecisionProgramBuilder.digestBytes(program.programDigest)
        let profileSHA = try OSDIPrecisionProgramBuilder.digestBytes(numericProfile)
        let directory = try await OSDIArtifactMaterializer(parent: stagingParentDirectory).materialize(worker, source: source)
        var native = cs_osdi_precision_process_options(maximum_mapping_bytes: options.maximumMappingBytes,
            arena_bytes: options.arenaBytes, maximum_work_units: options.maximumWorkUnits,
            startup_timeout_nanoseconds: options.startupTimeoutNanoseconds,
            evaluation_timeout_nanoseconds: options.evaluationTimeoutNanoseconds,
            cleanup_timeout_nanoseconds: options.cleanupTimeoutNanoseconds, poll_nanoseconds: options.pollNanoseconds,
            minimum_precision_bits: options.minimumPrecisionBits, maximum_precision_bits: options.maximumPrecisionBits,
            program_sha256: (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0),
            worker_profile_sha256: (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0))
        withUnsafeMutableBytes(of: &native.program_sha256) { $0.copyBytes(from: programSHA) }
        withUnsafeMutableBytes(of: &native.worker_profile_sha256) { $0.copyBytes(from: profileSHA) }
        var handle: OpaquePointer?, issue = cs_osdi_precision_process_issue()
        let status = directory.appendingPathComponent("model.osdi").path.withCString { path in
            program.programBytes.withUnsafeBufferPointer { bytes in program.setupBytes.withUnsafeBufferPointer { setup in
                cs_osdi_precision_process_create(path, bytes.baseAddress, UInt64(bytes.count), setup.baseAddress,
                    UInt64(setup.count), program.descriptorIndex, UInt32(program.topology.activeUnknownCount),
                    UInt32(program.topology.nodes.count), &native, cs_osdi_precision_swift_task_cancelled, nil, &handle, &issue)
            }}
        }
        guard status == 0, let handle else {
            let primary = OSDIError.precision(OSDIPrecisionNativeBridge.issue(issue))
            do { try OSDIPendingPrecisionCleanup(handle: handle, stagingDirectory: directory).close() }
            catch { throw .cleanup(primary: primary, secondary: error) }
            throw primary
        }
        return OSDIPrecisionSession(handle: handle, stagingDirectory: directory, program: program,
            workerReference: worker.expectedReference, numericProfile: numericProfile, options: options)
    }
}
