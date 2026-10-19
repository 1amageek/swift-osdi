import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts
@testable import OSDIRuntime
import Synchronization
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIControlledSnapshotTests {
    private enum Refusal: Error { case storage, work, cancelled }
    private final class Control: OSDIPrecisionSnapshotControlling {
        struct State {
            var live: UInt64 = 0, peak: UInt64 = 0, work: UInt64 = 0
            var reservations: [UInt64] = []
            var maximumStorage: UInt64 = .max, maximumWork: UInt64 = .max
            var cancelAt: UInt64?
        }
        let state = Mutex(State())
        func checkpoint(workUnits: UInt64) throws {
            try state.withLock { state in
                if let at = state.cancelAt, state.work >= at { throw Refusal.cancelled }
                guard workUnits <= state.maximumWork - state.work else { throw Refusal.work }
                state.work += workUnits
            }
        }
        func reserveStorage(byteCount: UInt64) throws -> any Sendable {
            try state.withLock { state in
                state.reservations.append(byteCount)
                guard byteCount <= state.maximumStorage - state.live else { throw Refusal.storage }
                state.live += byteCount; state.peak = max(state.peak, state.live)
            }
            return Receipt(control: self, bytes: byteCount)
        }
        private final class Receipt: Sendable {
            let control: Control, bytes: UInt64
            init(control: Control, bytes: UInt64) { self.control = control; self.bytes = bytes }
            deinit { control.state.withLock { $0.live -= bytes } }
        }
    }
    private func policy(mapping: UInt64 = 1_048_576) throws -> OSDIPrecisionOptions {
        try OSDIPrecisionOptions(maximumProgramBytes: 16_777_216, maximumMappingBytes: mapping,
            arenaBytes: 1_048_576, maximumWorkUnits: 10_000_000, minimumPrecisionBits: 53, maximumPrecisionBits: 256,
            startupTimeoutNanoseconds: 2_000_000_000, evaluationTimeoutNanoseconds: 2_000_000_000,
            cleanupTimeoutNanoseconds: 1_000_000_000, pollNanoseconds: 1_000_000)
    }
    private func preparation(_ parameters: [OSDIParameterAssignment] = []) throws -> OSDIPreparationOptions {
        try OSDIPreparationOptions(parameters: parameters, simulationParameters: [
            OSDISimulationParameter(name: "tnom", value: .real(300.15)),
            OSDISimulationParameter(name: "label", value: .string("exact UTF8 π"))],
            temperatureKelvin: 300.15, connectedTerminalCount: 1,
            limits: OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576,
                maximumDiagnosticCount: 16, maximumDiagnosticBytes: 1024, maximumStringBytes: 65_536))
    }
    private func fixture() throws -> (bytes: [UInt8], topology: OSDIPreparedTopology, reference: ArtifactReference, profile: ContentDigest) {
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let topology = try OSDIPreparedTopology(nodes: [OSDINodeInfo(index: 0, name: "p", units: "V", residualUnits: "A",
            isFlow: false, unknownNature: nature, residualNature: nature, activeUnknownIndex: 0)], activeUnknownCount: 1, stateCount: 0)
        let profile = try ContentDigest(algorithm: .sha256, hexadecimalValue: String(repeating: "0", count: 64))
        let reference = try ArtifactReference(digest: profile, byteCount: 1,
            descriptor: ArtifactDescriptor(role: .input, kind: .model, format: .raw))
        let nodes = MemoryLayout<CSOsdiPrecisionProgramHeader>.size
        let parameters = nodes + MemoryLayout<CSOsdiPrecisionNode>.size
        let strings = parameters + MemoryLayout<CSOsdiPrecisionParameter>.size
        var bytes = [UInt8](repeating: 0, count: strings + 9)
        var h = CSOsdiPrecisionProgramHeader()
        h.magic = (79,83,68,73,80,82,49,0); h.version = 1
        h.header_bytes = UInt32(nodes); h.program_bytes = UInt64(bytes.count)
        h.descriptor_index = 0; h.raw_node_count = 1; h.native_state_count = 0; h.flags = 1
        func empty<T>(_ type: T.Type) -> CSOsdiPrecisionSection { CSOsdiPrecisionSection(offset: 0, count: 0, stride: UInt32(MemoryLayout<T>.size)) }
        h.registers = empty(CSOsdiPrecisionRegister.self); h.constants = empty(CSOsdiPrecisionConstant.self)
        h.inputs = empty(CSOsdiPrecisionInput.self); h.functions = empty(CSOsdiPrecisionFunction.self)
        h.blocks = empty(CSOsdiPrecisionBlock.self); h.instructions = empty(CSOsdiPrecisionInstruction.self)
        h.phi_edges = empty(CSOsdiPrecisionPhiEdge.self); h.roots = empty(CSOsdiPrecisionRoot.self)
        h.nodes = CSOsdiPrecisionSection(offset: UInt64(nodes), count: 1, stride: UInt32(MemoryLayout<CSOsdiPrecisionNode>.size))
        h.parameters = CSOsdiPrecisionSection(offset: UInt64(parameters), count: 1, stride: UInt32(MemoryLayout<CSOsdiPrecisionParameter>.size))
        h.strings = CSOsdiPrecisionSection(offset: UInt64(strings), count: 9, stride: 1)
        var node = CSOsdiPrecisionNode(); node.osdi_index = 0; node.name_offset = 0; node.name_bytes = 1
        var parameter = CSOsdiPrecisionParameter(); parameter.osdi_index = 0; parameter.scope = 1
        parameter.type = 1; parameter.name_offset = 1; parameter.name_bytes = 8
        bytes.withUnsafeMutableBytes { out in
            out.storeBytes(of: h, toByteOffset: 0, as: CSOsdiPrecisionProgramHeader.self)
            out.storeBytes(of: node, toByteOffset: nodes, as: CSOsdiPrecisionNode.self)
            out.storeBytes(of: parameter, toByteOffset: parameters, as: CSOsdiPrecisionParameter.self)
        }
        bytes.replaceSubrange(strings..<bytes.count, with: "p$mfactor".utf8)
        return (bytes, topology, reference, profile)
    }
    private func build(control: Control, assignments: [OSDIParameterAssignment] = [], mapping: UInt64 = 1_048_576,
        metadataName: String = "$mfactor") throws -> OSDIPrecisionProgram {
        let f = try fixture(), options = try preparation(assignments)
        let input = try OSDIPrecisionSnapshotBuffer(count: f.bytes.count, control: control)
        input.withMutableBytes { out in f.bytes.withUnsafeBytes { out.copyMemory(from: $0) } }
        return try OSDIPrecisionControlledSnapshotBuilder.construct(program: input, modelReference: f.reference,
            descriptorIndex: 0, topology: f.topology,
            metadata: [OSDIParameterInfo(index: 0, names: [metadataName], kind: .instance, type: .real, arrayLength: 0)],
            preparation: options, semanticProfile: f.profile, options: policy(mapping: mapping), control: control)
    }
    @Test func fixedBackingSharesAddressesAndFinalCopyReleasesAdmission() throws {
        let control = Control()
        var first: OSDIPrecisionProgram? = try build(control: control)
        let live = control.state.withLock { $0.live }
        var second = first
        let p1 = first!.withProgramBytes { UInt(bitPattern: $0.baseAddress!) }
        let p2 = second!.withProgramBytes { UInt(bitPattern: $0.baseAddress!) }
        #expect(p1 == p2)
        #expect(first!.programBytes == second!.programBytes)
        #expect(first!.programDigest == (try OSDIPrecisionProgramBuilder.digest(first!.programBytes)))
        #expect(first!.setupDigest == (try OSDIPrecisionProgramBuilder.digest(first!.setupBytes)))
        first = nil
        #expect(control.state.withLock { $0.live } == live)
        #expect(second!.withProgramBytes { $0.count } > 0)
        second = nil
        #expect(control.state.withLock { $0.live } == 0)
    }
    @Test func preallocationMappingRefusalCancellationAndCumulativeWorkDoNotLeak() throws {
        let mapping = Control()
        #expect(throws: OSDIError.self) { try build(control: mapping, mapping: 1) }
        #expect(mapping.state.withLock { $0.live } == 0)
        // Only the exported program has been allocated; mapping refusal precedes slots/setup/hash.
        #expect(mapping.state.withLock { $0.reservations.count } == 1)
        let storage = Control(); storage.state.withLock { $0.maximumStorage = 0 }
        #expect(throws: Refusal.self) { try build(control: storage) }
        #expect(storage.state.withLock { $0.live } == 0)
        let cancelled = Control(); cancelled.state.withLock { $0.cancelAt = 25 }
        #expect(throws: Refusal.self) { try build(control: cancelled) }
        #expect(cancelled.state.withLock { $0.live } == 0)
        let cumulative = Control()
        do { let program = try build(control: cumulative); #expect(program.setupByteCount > 0) }
        let consumed = cumulative.state.withLock { $0.work }
        cumulative.state.withLock { $0.maximumWork = consumed }
        #expect(throws: Refusal.self) { try build(control: cumulative) }
        #expect(cumulative.state.withLock { $0.live } == 0)
    }
    @Test func scopedASCIIAndDuplicateRulesAreExplicit() throws {
        let control = Control()
        let uppercase = try OSDIParameterAssignment(name: "$MFACTOR", scope: .model, value: .real(0.75))
        do { let program = try build(control: control, assignments: [uppercase]); #expect(program.setupByteCount > 0) }
        #expect(control.state.withLock { $0.live } == 0)
        #expect(throws: OSDIError.self) { try build(control: control, assignments: [uppercase, uppercase]) }
        #expect(throws: OSDIError.self) { try build(control: control, metadataName: "µfactor") }
        #expect(control.state.withLock { $0.live } == 0)
    }
    private struct ActualRequest: Decodable {
        struct Model: Decodable {
            struct Simulation: Decodable { let name: String; let value: EvaluationParameterValue }
            struct Limits: Decodable {
                let maximumWorkspaceBytes, maximumDiagnosticBytes, maximumStringBytes: UInt64
                let maximumDiagnosticCount: UInt32
            }
            let libraryPath, expectedSHA256: String
            let expectedByteCount, descriptorIndex, connectedTerminalCount: Int
            let temperatureKelvin: Double
            let parameters: [EvaluationFixtureRequest.Parameter]
            let simulationParameters: [Simulation]
            let evaluationLimits: Limits
            func preparation() throws -> OSDIPreparationOptions {
                try OSDIPreparationOptions(parameters: parameters.map { try $0.assignment() },
                    simulationParameters: simulationParameters.map { try OSDISimulationParameter(name: $0.name, value: $0.value.value()) },
                    temperatureKelvin: temperatureKelvin, connectedTerminalCount: connectedTerminalCount,
                    limits: OSDIEvaluationLimits(maximumWorkspaceBytes: evaluationLimits.maximumWorkspaceBytes,
                        maximumDiagnosticCount: evaluationLimits.maximumDiagnosticCount,
                        maximumDiagnosticBytes: evaluationLimits.maximumDiagnosticBytes, maximumStringBytes: evaluationLimits.maximumStringBytes))
            }
        }
        struct Options: Decodable {
            let maximumProgramBytes, maximumMappingBytes, arenaBytes, maximumWorkUnits: UInt64
            let minimumPrecisionBits, maximumPrecisionBits: UInt32
            let startupTimeoutNanoseconds, evaluationTimeoutNanoseconds, cleanupTimeoutNanoseconds, pollNanoseconds: UInt64
            func policy() throws -> OSDIPrecisionOptions {
                try OSDIPrecisionOptions(maximumProgramBytes: maximumProgramBytes, maximumMappingBytes: maximumMappingBytes,
                    arenaBytes: arenaBytes, maximumWorkUnits: maximumWorkUnits, minimumPrecisionBits: minimumPrecisionBits,
                    maximumPrecisionBits: maximumPrecisionBits, startupTimeoutNanoseconds: startupTimeoutNanoseconds,
                    evaluationTimeoutNanoseconds: evaluationTimeoutNanoseconds, cleanupTimeoutNanoseconds: cleanupTimeoutNanoseconds,
                    pollNanoseconds: pollNanoseconds)
            }
        }
        let model: Model
        let semanticProfileSHA256: String
        let options: Options
    }
    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires exact original four-family request captures"))
    func actualFourPublicModelSnapshotsPreserveAllBytesAndRefund() async throws {
        let directory = try environment("OSDI_REAL_PRECISION_REQUEST_DIRECTORY")
        for family in ["bulk", "cmg", "img", "soi"] {
            let request = try JSONDecoder().decode(ActualRequest.self,
                from: Data(contentsOf: URL(fileURLWithPath: directory).appendingPathComponent("\(family)-request.json")))
            let artifact = try EvaluationArtifact(request.model.libraryPath), library = try await artifact.load()
            #expect(library.reference.digest.hexadecimalValue == request.model.expectedSHA256)
            #expect(library.reference.byteCount == request.model.expectedByteCount)
            let preparation = try request.model.preparation(), options = try request.options.policy()
            let profile = try ContentDigest(algorithm: .sha256, hexadecimalValue: request.semanticProfileSHA256)
            var model = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: request.model.descriptorIndex, options: preparation)
            let factory: any OSDIPrecisionSessionPreparing = OSDINativePrecisionSessionPreparer(), control = Control()
            let legacy = try factory.snapshot(library: library, descriptorIndex: request.model.descriptorIndex,
                model: model, preparation: preparation, semanticProfile: profile, options: options)
            var checked: OSDIPrecisionProgram? = try factory.snapshot(library: library, descriptorIndex: request.model.descriptorIndex,
                model: model, preparation: preparation, semanticProfile: profile, options: options, control: control)
            #expect(checked!.programBytes == legacy.programBytes, "program: \(family)")
            #expect(checked!.setupBytes == legacy.setupBytes, "setup: \(family)")
            #expect(checked!.programDigest == legacy.programDigest && checked!.setupDigest == legacy.setupDigest)
            model.close(); try library.close()
            #expect(checked!.withSetupBytes { $0.count } == legacy.setupBytes.count)
            checked = nil
            #expect(control.state.withLock { $0.live } == 0, "refund: \(family)")
        }
    }
    private func environment(_ key: String) throws -> String {
        guard let value = ProcessInfo.processInfo.environment[key], !value.isEmpty else { throw EvaluationFixtureError.requiredEnvironment(key) }
        return value
    }
    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires the exact compiler precision fixture"))
    func actualPublicSnapshotIsByteExactAndSurvivesLibraryClose() async throws {
        let artifact = try EvaluationArtifact(environment("OSDI_PRECISION_MODEL")), library = try await artifact.load()
        let options = try preparation(), profile = try ContentDigest(algorithm: .sha256,
            hexadecimalValue: environment("OSDI_PRECISION_SEMANTIC_SHA256"))
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        let control = Control(), factory: any OSDIPrecisionSessionPreparing = OSDINativePrecisionSessionPreparer()
        let legacy = try factory.snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: options, semanticProfile: profile, options: policy())
        var controlled: OSDIPrecisionProgram? = try factory.snapshot(library: library, descriptorIndex: 0, model: model,
            preparation: options, semanticProfile: profile, options: policy(), control: control)
        #expect(controlled!.programBytes == legacy.programBytes && controlled!.setupBytes == legacy.setupBytes)
        #expect(controlled!.programDigest == legacy.programDigest && controlled!.setupDigest == legacy.setupDigest)
        model.close(); try library.close()
        let worker = try EvaluationArtifact(environment("OSDI_PRECISION_WORKER"))
        let numeric = try ContentDigest(algorithm: .sha256,
            hexadecimalValue: environment("OSDI_PRECISION_NUMERIC_PROFILE_SHA256"))
        do {
            var session = try await factory.prepare(program: controlled!, worker: worker.intent, source: worker.source,
                numericProfile: numeric, options: policy())
            controlled = nil
            #expect(control.state.withLock { $0.live } > 0)
            try session.close()
            // A closed session still owns its immutable program until the value is destroyed.
            #expect(control.state.withLock { $0.live } > 0)
        }
        try await worker.source.close().wait()
        #expect(control.state.withLock { $0.live } == 0)
        let refused = Control(); refused.state.withLock { $0.maximumWork = 0 }
        // No old fallback: the mandatory control fails before any native snapshot operation.
        do {
            _ = try factory.snapshot(library: library, descriptorIndex: 0, model: model,
                preparation: options, semanticProfile: profile, options: policy(), control: refused)
            Issue.record("Closed model/control failure published a snapshot")
        } catch OSDIError.host(_, let underlying) { #expect(underlying is Refusal) }
        catch OSDIError.closed { }
    }
}
