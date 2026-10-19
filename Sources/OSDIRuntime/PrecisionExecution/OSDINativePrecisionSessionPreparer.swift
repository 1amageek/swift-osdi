import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts

/// Cold exact artifact acquisition and setup serialization; solver state remains caller-owned.
public struct OSDINativePrecisionSessionPreparer: OSDIPrecisionSessionPreparing, OSDIPrecisionRangeSessionPreparing, OSDIPrecisionChartSessionPreparing, OSDIPrecisionDecisionCoverSessionPreparing, OSDIPrecisionConstitutiveSessionPreparing {
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
        try await prepare(operation: .point, program: program, worker: worker, source: source,
            numericProfile: numericProfile, options: options)
    }
    public func prepareRange(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions) async throws(OSDIError) -> OSDIPrecisionSession {
        try await prepare(operation: .range, program: program, worker: worker, source: source,
            numericProfile: numericProfile, options: options)
    }
    public func prepareChart(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        limits: OSDIPrecisionChartLimits) async throws(OSDIPrecisionChartError) -> OSDIPrecisionSession {
        do {
            return try await prepare(operation: .chart, program: program, worker: worker, source: source,
                numericProfile: numericProfile, options: options, chartLimits: limits)
        } catch { throw .runtime(error) }
    }
    public func prepareDecisionCover(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        limits: OSDIPrecisionDecisionCoverLimits) async throws(OSDIPrecisionDecisionCoverError) -> OSDIPrecisionSession {
        do {
            return try await prepare(operation: .decisionCover, program: program, worker: worker, source: source,
                numericProfile: numericProfile, options: options, decisionCoverLimits: limits)
        } catch { throw .runtime(error) }
    }
    public func prepareConstitutiveIdentities(program: OSDIPrecisionProgram, worker: ArtifactAccessIntent,
        source: any ArtifactAccessing, numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        coverLimits: OSDIPrecisionDecisionCoverLimits, limits: OSDIPrecisionConstitutiveLimits,
        coordinateQuotient: OSDIPrecisionCoordinateQuotient)
        async throws(OSDIPrecisionConstitutiveError) -> OSDIPrecisionSession {
        do {
            return try await prepare(operation: .constitutiveIdentities, program: program, worker: worker, source: source,
                numericProfile: numericProfile, options: options, decisionCoverLimits: coverLimits, constitutiveLimits: limits, coordinateQuotient: coordinateQuotient)
        } catch { throw .runtime(error) }
    }
    private func prepare(operation: OSDIPrecisionSession.Operation, program: OSDIPrecisionProgram,
        worker: ArtifactAccessIntent, source: any ArtifactAccessing,
        numericProfile: ContentDigest, options: OSDIPrecisionOptions,
        chartLimits: OSDIPrecisionChartLimits? = nil,
        decisionCoverLimits: OSDIPrecisionDecisionCoverLimits? = nil,
        constitutiveLimits: OSDIPrecisionConstitutiveLimits? = nil,
        coordinateQuotient: OSDIPrecisionCoordinateQuotient? = nil) async throws(OSDIError) -> OSDIPrecisionSession {
        guard (operation == .chart) == (chartLimits != nil),
              ((operation == .decisionCover || operation == .constitutiveIdentities) == (decisionCoverLimits != nil)),
              (operation == .constitutiveIdentities) == (constitutiveLimits != nil),
              (operation == .constitutiveIdentities) == (coordinateQuotient != nil) else { throw .invalidRange }
        do { try coordinateQuotient?.validate(for: program.topology) }
        catch { throw .contract(error) }
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
                if (operation == .decisionCover || operation == .constitutiveIdentities), let decisionCoverLimits {
                    var limits = cs_osdi_cover_limits_v4(maximum_steps: decisionCoverLimits.maximumSteps,
                        maximum_guard_records: decisionCoverLimits.maximumGuardRecords, maximum_copied_bytes: decisionCoverLimits.maximumCopiedBytes,
                        maximum_cells: decisionCoverLimits.maximumCells, maximum_nodes: decisionCoverLimits.maximumNodes,
                        maximum_leaves: decisionCoverLimits.maximumLeaves, maximum_pending: decisionCoverLimits.maximumPending,
                        maximum_depth: decisionCoverLimits.maximumDepth, maximum_integer_bits: decisionCoverLimits.maximumIntegerBits, reserved: 0)
                    if let constitutiveLimits, let coordinateQuotient {
                        var identities = cs_osdi_identity_limits_v5(maximum_nodes: constitutiveLimits.maximumNodes,
                            maximum_atoms: constitutiveLimits.maximumAtoms, maximum_coefficient_bits: constitutiveLimits.maximumCoefficientBits,
                            maximum_workspace_bytes: constitutiveLimits.maximumWorkspaceBytes)
                        return coordinateQuotient.representatives.withUnsafeBufferPointer { quotient in
                            cs_osdi_identity_process_create(path, bytes.baseAddress, UInt64(bytes.count), setup.baseAddress,
                            UInt64(setup.count), program.descriptorIndex, UInt32(program.topology.activeUnknownCount), UInt32(program.topology.nodes.count),
                            &native, &limits, &identities, quotient.baseAddress, UInt64(quotient.count),
                            cs_osdi_precision_swift_task_cancelled, nil, &handle, &issue)
                        }
                    }
                    return cs_osdi_cover_process_create(path, bytes.baseAddress, UInt64(bytes.count), setup.baseAddress,
                        UInt64(setup.count), program.descriptorIndex, UInt32(program.topology.activeUnknownCount), UInt32(program.topology.nodes.count),
                        &native, &limits, cs_osdi_precision_swift_task_cancelled, nil, &handle, &issue)
                }
                if operation == .chart, let chartLimits {
                    var limits = cs_osdi_chart_limits_v3(maximum_steps: chartLimits.maximumSteps,
                        maximum_tape_entries: chartLimits.maximumTapeEntries, maximum_guard_records: chartLimits.maximumGuardRecords)
                    return cs_osdi_chart_process_create(path, bytes.baseAddress, UInt64(bytes.count), setup.baseAddress,
                        UInt64(setup.count), program.descriptorIndex, UInt32(program.topology.activeUnknownCount),
                        UInt32(program.topology.nodes.count), &native, &limits,
                        cs_osdi_precision_swift_task_cancelled, nil, &handle, &issue)
                }
                let create = operation == .point ? cs_osdi_precision_process_create : cs_osdi_range_process_create
                return create(path, bytes.baseAddress, UInt64(bytes.count), setup.baseAddress,
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
            workerReference: worker.expectedReference, numericProfile: numericProfile, options: options, operation: operation, chartLimits: chartLimits, decisionCoverLimits: decisionCoverLimits, constitutiveLimits: constitutiveLimits, coordinateQuotient: coordinateQuotient)
    }
}
