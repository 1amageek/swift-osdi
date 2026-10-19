import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts
import OSDIRuntime

/// Public snapshot-to-identity integration for one stateless analytic source.
enum PrecisionConsumerProbe {
    struct Snapshot: Codable, Equatable {
        let programDigest: String
        let setupDigest: String
        let semanticDigest: String
        let profileDigest: String
        let representatives: [UInt32]
        let kinds: [UInt32]
        let coefficients: [UInt64]
        let wrongOperationRefused: Bool
        let limitRefused: Bool
        let closedRefused: Bool
    }
    private struct Observation: Equatable {
        let kinds: [UInt32]
        let coefficients: [UInt64]
        let address: UInt
        let valid: Bool
    }
    private static func options() throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 1_048_576, maximumMappingBytes: 16_777_216,
            arenaBytes: 16_777_216, maximumWorkUnits: 100_000_000, minimumPrecisionBits: 128, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private static func coverLimits() throws -> OSDIPrecisionDecisionCoverLimits {
        try OSDIPrecisionDecisionCoverLimits(maximumSteps: 100_000, maximumGuardRecords: 100_000,
            maximumCopiedBytes: 10_000_000, maximumCells: 100, maximumNodes: 256, maximumLeaves: 64,
            maximumPending: 256, maximumDepth: 256, maximumIntegerBits: 1025)
    }
    private static func limits(nodes: UInt32 = 256) throws -> OSDIPrecisionConstitutiveLimits {
        try OSDIPrecisionConstitutiveLimits(maximumNodes: nodes, maximumAtoms: 64,
            maximumCoefficientBits: 4096, maximumWorkspaceBytes: 2_097_152)
    }
    private static func workerInput(_ file: URL) throws -> (ArtifactRootCapability, ArtifactAccessIntent) {
        let directory = file.deletingLastPathComponent()
        let reference = try LocalArtifactReferencer(digester: SHA256ContentDigester()).reference(
            ArtifactLocator(location: ArtifactLocation(workspaceRelativePath: file.lastPathComponent), role: .input,
                kind: .model, format: .raw), relativeTo: directory)
        let rootID = try ArtifactRootID(rawValue: "external-precision-worker")
        let budget = try ArtifactAccessBudget(maximumPageByteCount: 65_536, maximumTotalByteCount: 16_777_216,
            maximumPageCount: 4096, maximumWorkUnitCount: 8192, maximumDurationNanoseconds: 5_000_000_000)
        let intent = try ArtifactAccessIntent(expectedReference: reference,
            availability: .local(artifactID: reference.id, rootID: rootID,
                relativePath: ArtifactRelativePath(segments: [file.lastPathComponent])), operation: .read, budget: budget)
        return (try ArtifactRootCapability(rootID: rootID, directoryURL: directory, digester: SHA256ContentDigester()), intent)
    }
    static func exercise(libraryFile: URL, workerFile: URL, profile: ContentDigest,
        sourceDigest: ContentDigest, semanticProfile: ContentDigest, staging: URL) async throws -> Snapshot {
        let library = try await ConsumerEntry.load(libraryFile, staging: staging)
        let result: Snapshot
        do { result = try await probe(library: library, workerFile: workerFile, profile: profile, sourceDigest: sourceDigest, semanticProfile: semanticProfile, staging: staging) }
        catch {
            do { try library.close() } catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
            throw error
        }
        try library.close(); return result
    }
    private static func probe(library: borrowing OSDILibrary, workerFile: URL, profile: ContentDigest,
        sourceDigest: ContentDigest, semanticProfile: ContentDigest, staging: URL) async throws -> Snapshot {
        let preparation = try OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
            connectedTerminalCount: 1, limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 8_388_608,
                maximumDiagnosticCount: 256, maximumDiagnosticBytes: 65_536, maximumStringBytes: 65_536))
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
        defer { model.close() }
        let factory: any OSDIPrecisionConstitutiveSessionPreparing = OSDINativePrecisionSessionPreparer(stagingParentDirectory: staging)
        let program = try OSDINativePrecisionSessionPreparer().snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: preparation, semanticProfile: semanticProfile, options: options())
        guard program.programDigest == sourceDigest, program.semanticProfile == semanticProfile, program.modelReference == library.reference,
              program.topology.stateCount == 0, program.topology.activeUnknownCount == 1 else {
            throw ConsumerFailure.invariant("Actual public precision snapshot binding")
        }
        let quotient = try OSDIPrecisionCoordinateQuotient(topology: program.topology, representatives: [UInt32.max])
        let (source, intent) = try workerInput(workerFile)
        let result: Result<Snapshot, any Error>
        do {
            var session = try await factory.prepareConstitutiveIdentities(program: program, worker: intent, source: source,
                numericProfile: profile, options: options(), coverLimits: coverLimits(), limits: limits(), coordinateQuotient: quotient)
            let first: Observation
            do {
                let low = [-1.0], high = [1.0], zero = [0.0]
                let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0)
                var called = false
                do {
                    try OSDINativePrecisionEvaluator().constructDecisionCover(session: &session, lowerHigh: low.span, lowerLow: zero.span,
                        upperHigh: high.span, upperLow: zero.span, lowerContext: context, upperContext: context, precisionBits: 192) { _ in called = true }
                    throw ConsumerFailure.invariant("Wrong operation succeeded")
                } catch OSDIPrecisionDecisionCoverError.runtime(.invalidRange) {}
                guard !called else { throw ConsumerFailure.invariant("Wrong operation published") }
                first = try observe(&session)
                let second = try observe(&session)
                guard first.valid, first == second, first.kinds == [1,2], first.coefficients == [1,1], first.address != 0 else {
                    throw ConsumerFailure.invariant("Borrowed conditional identity or address reuse")
                }
            } catch {
                do { try session.close() } catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
                throw error
            }
            try session.close(); try session.close()
            do { _ = try observe(&session); throw ConsumerFailure.invariant("Closed session succeeded") }
            catch OSDIPrecisionConstitutiveError.runtime(.closed) {}
            var limited = try await factory.prepareConstitutiveIdentities(program: program, worker: intent, source: source,
                numericProfile: profile, options: options(), coverLimits: coverLimits(), limits: limits(nodes: 1), coordinateQuotient: quotient)
            var limitPublished = false
            do { _ = try observe(&limited) { limitPublished = true }; throw ConsumerFailure.invariant("Identity node limit succeeded") }
            catch OSDIPrecisionConstitutiveError.capacityExceeded(let issue) {
                guard issue.workerStatus == 121 else { throw ConsumerFailure.invariant("Wrong identity limit status") }
            } catch {
                do { try limited.close() } catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
                throw error
            }
            guard !limitPublished else { throw ConsumerFailure.invariant("Limited session published a claim") }
            try limited.close(); try limited.close()
            result = .success(Snapshot(programDigest: program.programDigest.hexadecimalValue, setupDigest: program.setupDigest.hexadecimalValue, semanticDigest: program.semanticProfile.hexadecimalValue,
                profileDigest: profile.hexadecimalValue, representatives: quotient.representatives, kinds: first.kinds,
                coefficients: first.coefficients, wrongOperationRefused: true, limitRefused: true, closedRefused: true))
        } catch { result = .failure(error) }
        do { try await source.close().wait() }
        catch {
            if case .failure(let primary) = result { throw ConsumerFailure.cleanup(primary: primary, secondary: error) }
            throw error
        }
        return try result.get()
    }
    private static func observe(_ session: inout OSDIPrecisionSession, onPublication: () -> Void = {}) throws -> Observation {
        let low = [-1.0], high = [1.0], zero = [0.0]
        let context = try OSDIPrecisionContext(mode: .transient, timeHigh: 0.25, timeLow: 0)
        let expectedBinding = session.chartBinding
        return try OSDINativePrecisionEvaluator().constructConstitutiveIdentities(session: &session,
            lowerHigh: low.span, lowerLow: zero.span, upperHigh: high.span, upperLow: zero.span,
            lowerContext: context, upperContext: context, precisionBits: 192) { view in
                onPublication()
                var kinds: [UInt32] = [], coefficients: [UInt64] = [], address: UInt = 0
                var valid = view.binding.source == expectedBinding && view.binding.coordinateQuotient.representatives == [UInt32.max]
                valid = valid && view.cover.lowerHigh[0] == -1 && view.cover.upperHigh[0] == 1 && view.cover.leafCount == 1
                for i in 0..<view.claimCount { view.withClaim(at: i) { claim in
                    valid = valid && claim.rawRowCount == 1 && claim.binding == view.binding; kinds.append(claim.kind.rawValue)
                    claim.withCoefficient(at: 0) { value in
                        let count = value.numeratorWords.count
                        valid = valid && !value.negative && value.denominatorPower == 0 && count == 1
                        coefficients.append(count == 1 ? value.numeratorWords[0] : 0)
                        if i == 0 { address = value.numeratorWords.withUnsafeBufferPointer { UInt(bitPattern: $0.baseAddress!) } }
                    }
                }}
                return Observation(kinds: kinds, coefficients: coefficients, address: address, valid: valid)
            }
    }
}
