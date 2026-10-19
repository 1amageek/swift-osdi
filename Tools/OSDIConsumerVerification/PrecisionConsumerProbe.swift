import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import Foundation
import OSDIContracts
import OSDIRuntime
import Synchronization

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
        let programByteCount: Int
        let setupByteCount: Int
        let cumulativeWork: UInt64
        let peakRetainedBytes: UInt64
        let copiedRetainedBytes: UInt64
        let closedSessionRetainedBytes: UInt64
        let finalRetainedBytes: UInt64
        let controlledRefusals: [String]
    }
    private enum ControlRefusal: String, Error { case work, storage, cancelled }
    private final class Control: OSDIPrecisionSnapshotControlling {
        struct Usage { var work: UInt64 = 0, live: UInt64 = 0, peak: UInt64 = 0 }
        private let state = Mutex(Usage())
        private let maximumWork: UInt64
        private let maximumStorage: UInt64
        private let cancelled: Bool
        init(maximumWork: UInt64 = 100_000_000, maximumStorage: UInt64 = 67_108_864, cancelled: Bool = false) {
            self.maximumWork = maximumWork; self.maximumStorage = maximumStorage; self.cancelled = cancelled
        }
        var usage: Usage { state.withLock { $0 } }
        func checkpoint(workUnits: UInt64) throws {
            try state.withLock { usage in
                if cancelled { throw ControlRefusal.cancelled }
                guard workUnits <= maximumWork - usage.work else { throw ControlRefusal.work }
                usage.work += workUnits
            }
        }
        func reserveStorage(byteCount: UInt64) throws -> any Sendable {
            try state.withLock { usage in
                if cancelled { throw ControlRefusal.cancelled }
                guard byteCount <= maximumStorage - usage.live else { throw ControlRefusal.storage }
                usage.live += byteCount; usage.peak = max(usage.peak, usage.live)
            }
            return Receipt(control: self, bytes: byteCount)
        }
        private final class Receipt: Sendable {
            let control: Control
            let bytes: UInt64
            init(control: Control, bytes: UInt64) { self.control = control; self.bytes = bytes }
            deinit { control.state.withLock { $0.live -= bytes } }
        }
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
        let control = Control()
        let result: Snapshot
        do { result = try await probe(library: library, workerFile: workerFile, profile: profile, sourceDigest: sourceDigest, semanticProfile: semanticProfile, staging: staging, control: control) }
        catch {
            do { try library.close() } catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
            throw error
        }
        try library.close()
        guard control.usage.live == 0, control.usage.work == result.cumulativeWork else {
            throw ConsumerFailure.invariant("Controlled snapshot final refund or cumulative work")
        }
        return result
    }
    private static func probe(library: borrowing OSDILibrary, workerFile: URL, profile: ContentDigest,
        sourceDigest: ContentDigest, semanticProfile: ContentDigest, staging: URL, control: Control) async throws -> Snapshot {
        let preparation = try OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
            connectedTerminalCount: 1, limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 8_388_608,
                maximumDiagnosticCount: 256, maximumDiagnosticBytes: 65_536, maximumStringBytes: 65_536))
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: preparation)
        defer { model.close() }
        let factory: any OSDIPrecisionConstitutiveSessionPreparing = OSDINativePrecisionSessionPreparer(stagingParentDirectory: staging)
        let snapshotFactory: any OSDIPrecisionSessionPreparing = OSDINativePrecisionSessionPreparer()
        let legacy = try snapshotFactory.snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: preparation, semanticProfile: semanticProfile, options: options())
        var original: OSDIPrecisionProgram? = try snapshotFactory.snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: preparation, semanticProfile: semanticProfile, options: options(), control: control)
        let retained = control.usage.live
        var program = original
        guard withExtendedLifetime(original, { control.usage.live == retained }) else {
            throw ConsumerFailure.invariant("Controlled value copy duplicated retained admission")
        }
        original = nil
        guard retained > 0, control.usage.live == retained else {
            throw ConsumerFailure.invariant("Controlled value copy changed retained admission")
        }
        try compareBytes(program!, legacy: legacy, control: control)
        guard program!.programDigest == sourceDigest, program!.semanticProfile == semanticProfile, program!.modelReference == library.reference,
              program!.topology.stateCount == 0, program!.topology.activeUnknownCount == 1 else {
            throw ConsumerFailure.invariant("Actual public precision snapshot binding")
        }
        for reason in [ControlRefusal.work, .storage, .cancelled] {
            let refused = Control(maximumWork: reason == .work ? 0 : 100_000_000,
                maximumStorage: reason == .storage ? 0 : 67_108_864, cancelled: reason == .cancelled)
            do {
                _ = try snapshotFactory.snapshot(library: library, descriptorIndex: 0, model: model,
                    preparation: preparation, semanticProfile: semanticProfile, options: options(), control: refused)
                throw ConsumerFailure.invariant("Controlled refusal published a snapshot")
            } catch OSDIError.host(let operation, let underlying) {
                guard operation == "controlled precision snapshot", let actual = underlying as? ControlRefusal, actual == reason,
                      refused.usage.live == 0 else { throw ConsumerFailure.invariant("Controlled refusal lost its cause or reservation") }
            }
        }
        let programBytes = program!.programByteCount, setupBytes = program!.setupByteCount
        let programDigest = program!.programDigest, setupDigest = program!.setupDigest
        let quotient = try OSDIPrecisionCoordinateQuotient(topology: program!.topology, representatives: [UInt32.max])
        model.close(); try library.close()
        let (source, intent) = try workerInput(workerFile)
        let result: Result<Snapshot, any Error>
        do {
            try await checkLimit(factory: factory, program: program!, worker: intent, source: source, profile: profile, quotient: quotient)
            var session = try await factory.prepareConstitutiveIdentities(program: program!, worker: intent, source: source,
                numericProfile: profile, options: options(), coverLimits: coverLimits(), limits: limits(), coordinateQuotient: quotient)
            program = nil
            let first: Observation
            do {
                guard control.usage.live == retained else { throw ConsumerFailure.invariant("Session did not retain controlled snapshot") }
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
            guard control.usage.live == retained else { throw ConsumerFailure.invariant("Closed session released its immutable snapshot prematurely") }
            do { _ = try observe(&session); throw ConsumerFailure.invariant("Closed session succeeded") }
            catch OSDIPrecisionConstitutiveError.runtime(.closed) {}
            let usage = withExtendedLifetime(session) { control.usage }
            result = .success(Snapshot(programDigest: programDigest.hexadecimalValue, setupDigest: setupDigest.hexadecimalValue, semanticDigest: semanticProfile.hexadecimalValue,
                profileDigest: profile.hexadecimalValue, representatives: quotient.representatives, kinds: first.kinds,
                coefficients: first.coefficients, wrongOperationRefused: true, limitRefused: true, closedRefused: true,
                programByteCount: programBytes, setupByteCount: setupBytes, cumulativeWork: usage.work,
                peakRetainedBytes: usage.peak, copiedRetainedBytes: retained, closedSessionRetainedBytes: usage.live,
                finalRetainedBytes: 0, controlledRefusals: ["work", "storage", "cancelled"]))
        } catch { result = .failure(error) }
        do { try await source.close().wait() }
        catch {
            if case .failure(let primary) = result { throw ConsumerFailure.cleanup(primary: primary, secondary: error) }
            throw error
        }
        return try result.get()
    }
    private static func compareBytes(_ program: OSDIPrecisionProgram, legacy: OSDIPrecisionProgram, control: Control) throws {
        let (count, overflow) = UInt64(program.programByteCount).addingReportingOverflow(UInt64(program.setupByteCount))
        let (copies, copyOverflow) = count.multipliedReportingOverflow(by: 2)
        guard !overflow, !copyOverflow else { throw ConsumerFailure.invariant("Snapshot comparison capacity overflow") }
        try control.checkpoint(workUnits: copies)
        let reservation = try control.reserveStorage(byteCount: copies)
        try withExtendedLifetime(reservation) {
            guard program.programByteCount == legacy.programByteCount, program.setupByteCount == legacy.setupByteCount,
                  program.programBytes == legacy.programBytes, program.setupBytes == legacy.setupBytes,
                  program.programDigest == legacy.programDigest, program.setupDigest == legacy.setupDigest else {
                throw ConsumerFailure.invariant("Controlled snapshot differs from actual legacy bytes/counts/digests")
            }
        }
    }
    private static func checkLimit(factory: any OSDIPrecisionConstitutiveSessionPreparing, program: OSDIPrecisionProgram,
        worker: ArtifactAccessIntent, source: any ArtifactAccessing, profile: ContentDigest,
        quotient: OSDIPrecisionCoordinateQuotient) async throws {
        var limited = try await factory.prepareConstitutiveIdentities(program: program, worker: worker, source: source,
            numericProfile: profile, options: options(), coverLimits: coverLimits(), limits: limits(nodes: 1), coordinateQuotient: quotient)
        var published = false
        do {
            do { _ = try observe(&limited) { published = true }; throw ConsumerFailure.invariant("Identity node limit succeeded") }
            catch OSDIPrecisionConstitutiveError.capacityExceeded(let issue) {
                guard issue.workerStatus == 121 else { throw ConsumerFailure.invariant("Wrong identity limit status") }
            }
            guard !published else { throw ConsumerFailure.invariant("Limited session published a claim") }
        } catch {
            do { try limited.close() } catch let secondary { throw ConsumerFailure.cleanup(primary: error, secondary: secondary) }
            throw error
        }
        try limited.close(); try limited.close()
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
