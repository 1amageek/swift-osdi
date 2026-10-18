import CircuiteFoundation
import CircuiteFoundationCrypto
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import COSDI
import Foundation
import OSDIContracts
import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIRuntimeTests {
    @Test
    func admitsTwoDescriptorsAndExtendedStride() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        var ordinaryStride: UInt64 = 0
        for name in ["valid_two", "extended_stride", "valid_ground_pair"] {
            let before = try fixtures.lifetime(name).count
            let library = try await load(try fixtures.library(name))
            #expect(library.descriptors.map(\.name) == ["metadata_first", "metadata_second"])
            #expect(library.descriptors.map(\.index) == [0, 1])
            #expect(library.descriptors.allSatisfy { $0.nodeCount == 2 && $0.terminalCount == 2 })
            #expect(library.descriptors.allSatisfy { $0.modelByteCount == 16 && $0.instanceByteCount == 64 })
            if name == "valid_two" { ordinaryStride = library.descriptorStride }
            if name == "extended_stride" { #expect(library.descriptorStride == ordinaryStride + 16) }
            if name == "valid_ground_pair" { #expect(library.descriptors.allSatisfy { $0.collapsibleCount == 1 }) }
            #expect(Array(try fixtures.lifetime(name).dropFirst(before)) == ["load"])
            try library.close()
            #expect(Array(try fixtures.lifetime(name).dropFirst(before)) == ["load", "unload"])
            // Metadata is copied and remains meaningful after the image unloads.
            #expect(library.descriptors[1].name == "metadata_second")
        }
    }

    @Test
    func admitsOptimizedEntriesWithOverprovisionedJacobianCapacity() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        for name in ["valid_overprovisioned_jacobian_capacity", "valid_capacity_above_final_jacobian_count"] {
            let before = try fixtures.lifetime(name).count
            let library = try await load(try fixtures.library(name))
            #expect(library.descriptors.map(\.name) == ["metadata_first", "metadata_second"])
            #expect(library.descriptors.allSatisfy { $0.jacobianEntryCount == 2 && $0.instanceByteCount == 80 })
            #expect(Array(try fixtures.lifetime(name).dropFirst(before)) == ["load"])
            try library.close()
            #expect(Array(try fixtures.lifetime(name).dropFirst(before)) == ["load", "unload"])
        }
    }

    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires explicit OSDI_MODEL_QUALIFICATION=1 and exact external compiler/model inputs"))
    func admitsActualOpenVAFResistiveAndReactiveModels() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        for (name, expected) in [("linear_rc", ["linear_rc"]), ("two_models", ["first_rc", "second_rc"])] {
            let library = try await load(try fixtures.model(name))
            #expect(library.descriptors.map(\.name) == expected)
            #expect(library.descriptors.allSatisfy {
                $0.nodeCount == 2 && $0.terminalCount == 2 && $0.jacobianEntryCount > 0
                    && $0.instanceByteCount > 0 && $0.modelByteCount > 0
            })
            try library.close()
        }
    }

    @Test
    func rejectsMalformedDescriptorsWithTypedDiagnosticsAndUnloads() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let cases: [(String, Int32, String)] = [
            ("wrong_major", 5, "version_or_stride"),
            ("wrong_minor", 5, "version_or_stride"),
            ("missing_required_symbol", 4, "symbols"),
            ("undersized_stride", 5, "version_or_stride"),
            ("excessive_descriptors", 7, "descriptor_count"),
            ("excessive_nodes", 7, "nodes"),
            ("invalid_mapping_offset", 6, "node_mapping_offset"),
            ("overflow_jacobian_count", 7, "jacobian_entries"),
            ("misaligned_mapping_offset", 6, "node_mapping_offset"),
            ("invalid_jacobian_node", 6, "jacobian_entries.nodes"),
            ("invalid_terminal_count", 6, "counts"),
            ("null_nodes", 6, "nodes"),
            ("missing_eval", 6, "eval"),
            ("invalid_residual_offset", 6, "nodes.resist_residual_off"),
            ("misaligned_jacobian_pointer", 6, "jacobian_entries.react_ptr_off"),
            ("null_jacobian_entries", 6, "jacobian_entries"),
            ("excessive_storage", 7, "storage"),
            ("invalid_ground_first_node", 6, "collapsible"),
            ("missing_setup_model", 6, "setup_model"),
            ("underprovided_jacobian_capacity", 6, "jacobian_counts"),
            ("excessive_jacobian_capacity", 7, "jacobian_counts")
        ]
        for (name, code, field) in cases {
            let before = try fixtures.lifetime(name).count
            let error = try await refusedLoad(try fixtures.library(name))
            guard case .native(let issue) = error else {
                Issue.record("Expected native refusal for \(name), received \(error)")
                continue
            }
            #expect(issue.code == code, "\(name)")
            #expect(issue.field == field, "\(name)")
            #expect(!issue.detail.isEmpty)
            #expect(issue.descriptorIndex == (code == 4 || code == 5 || name == "excessive_descriptors" ? UInt32.max : 0))
            #expect(Array(try fixtures.lifetime(name).dropFirst(before)) == ["load", "unload"])
        }
    }

    @Test
    func rejectsWrongArchitectureBeforeConstructor() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let name = "wrong_architecture"
        let before = try fixtures.lifetime(name)
        let error = try await refusedLoad(try fixtures.library(name))
        guard case .native(let issue) = error else { throw TestFailure.unexpectedError(error) }
        #expect(issue.code == 2)
        #expect(issue.field == "architecture")
        #expect(try fixtures.lifetime(name) == before)
    }

    @Test
    func rejectsRequestedDescriptorMetadataAndStorageBudgets() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let path = try fixtures.library("valid_two")
        let cases: [(OSDILoadLimits, String)] = [
            (try OSDILoadLimits(maximumDescriptors: 1, maximumMetadataBytes: 1_048_576,
                               maximumStorageBytes: 1_048_576), "descriptor_count"),
            (try OSDILoadLimits(maximumDescriptors: 16, maximumMetadataBytes: 8,
                               maximumStorageBytes: 1_048_576), "descriptors"),
            (try OSDILoadLimits(maximumDescriptors: 16, maximumMetadataBytes: 1_048_576,
                               maximumStorageBytes: 79), "storage")
        ]
        for (limits, field) in cases {
            let error = try await refusedLoad(path, limits: limits)
            guard case .native(let issue) = error else { throw TestFailure.unexpectedError(error) }
            #expect(issue.code == 7)
            #expect(issue.field == field)
        }
    }

    @Test
    func stableBlocksAreZeroedPreserveWritesAndPropagateTypedCallbackErrors() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let library = try await load(try fixtures.library("valid_two"))
        try exerciseStorage(library)
        try library.close()
    }

    @Test
    func rejectsInvalidIndicesAndNewStorageAfterIdempotentClose() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let library = try await load(try fixtures.library("valid_two"))
        for index in [-1, 2, Int.max] {
            do {
                let storage = try library.makeStorage(descriptorIndex: index)
                _ = storage.descriptor
                Issue.record("Invalid descriptor index admitted: \(index)")
            } catch {
                guard case .invalidRange = error else { throw error }
            }
        }
        try library.close()
        try library.close()
        do {
            let storage = try library.makeStorage(descriptorIndex: 0)
            _ = storage.descriptor
            Issue.record("Closed library admitted a new storage lease")
        } catch {
            guard case .closed = error else { throw error }
        }
    }

    @Test
    func storageRetainsImageAfterPublicLibraryValueIsDropped() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let before = try fixtures.lifetime("valid_two").count
        do {
            var storage = try await storageWithoutLibrary(try fixtures.library("valid_two"))
            #expect(Array(try fixtures.lifetime("valid_two").dropFirst(before)) == ["load"])
            storage.withInstanceBytes { bytes in
                #expect(bytes.count == 64)
                bytes[0] = 19
            }
            #expect(storage.descriptor.name == "metadata_first")
        }
        #expect(Array(try fixtures.lifetime("valid_two").dropFirst(before)) == ["load", "unload"])
    }

    @Test
    func concurrentMetadataReadsAndCloseRespectLiveStorageLease() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let library = try await load(try fixtures.library("valid_two"))
        let barrier = LeaseBarrier()
        let holder = Task {
            var storage = try library.makeStorage(descriptorIndex: 1)
            await barrier.announceLease()
            await barrier.waitForRelease()
            storage.withModelBytes { $0[0] = 23 }
            return storage.descriptor.name
        }
        await barrier.waitForLease()
        let reader = Task.detached {
            for _ in 0..<1000 {
                #expect(library.descriptors[0].name == "metadata_first")
                #expect(library.descriptors[1].instanceByteCount == 64)
            }
        }
        do {
            try library.close()
            Issue.record("Close succeeded while another task held its storage lease")
        } catch {
            guard case .libraryInUse = error else {
                await barrier.release()
                _ = try await holder.value
                throw error
            }
        }
        await reader.value
        await barrier.release()
        #expect(try await holder.value == "metadata_second")
        try library.close()
        #expect(library.descriptors[1].name == "metadata_second")
    }

    @Test
    func artifactDigestTamperFailsBeforeNativeConstructor() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let before = try fixtures.lifetime("valid_two")
        let input = try ArtifactInput(copying: fixtures.library("valid_two"))
        var bytes = try Data(contentsOf: input.file)
        bytes[bytes.count - 1] ^= 1
        try bytes.write(to: input.file)
        let loadLimits = try limits()
        let result: Result<OSDILibrary, OSDIError>
        do { result = .success(try await input.loader.load(input.intent, source: input.root, limits: loadLimits)) }
        catch { result = .failure(error) }
        try await input.root.close().wait()
        switch result {
        case .success(let library):
            try library.close()
            Issue.record("Tampered artifact was admitted")
        case .failure(.artifact(.contentDigestMismatch)):
            break
        case .failure(let error):
            throw TestFailure.unexpectedError(error)
        }
        #expect(try fixtures.lifetime("valid_two") == before)
    }

    @Test
    func preservesReadFailureCloseFailureAndCombinedCleanupFailure() async throws {
        let fixtures = try await OSDIFixtureCache.shared.fixtures()
        let before = try fixtures.lifetime("valid_two")
        for (readFailure, closeFailure) in [(true, false), (false, true), (true, true)] {
            let input = try ArtifactInput(copying: fixtures.library("valid_two"))
            let source = FaultSource(base: input.root, readFailure: readFailure, closeFailure: closeFailure)
            let loadLimits = try limits()
            let result: Result<OSDILibrary, OSDIError>
            do { result = .success(try await input.loader.load(input.intent, source: source, limits: loadLimits)) }
            catch { result = .failure(error) }
            try await input.root.close().wait()
            guard case .failure(let error) = result else {
                if case .success(let library) = result { try library.close() }
                Issue.record("Injected artifact session failure was admitted")
                continue
            }
            if readFailure && closeFailure {
                guard case .cleanup(let primary, let secondary) = error else { throw TestFailure.unexpectedError(error) }
                expectReadFailure(primary)
                expectCloseFailure(secondary)
            } else if readFailure { expectReadFailure(error) }
            else { expectCloseFailure(error) }
        }
        #expect(try fixtures.lifetime("valid_two") == before)
    }

    private func exerciseStorage(_ library: OSDILibrary) throws {
        var storage = try library.makeStorage(descriptorIndex: 0)
        #expect(storage.descriptor == library.descriptors[0])
        let firstModelAddress = storage.withModelBytes { bytes -> UInt in
            #expect(bytes.count == 16)
            for index in 0..<bytes.count { #expect(bytes[index] == 0) }
            bytes[0] = 41
            bytes[bytes.count - 1] = 43
            return bytes.withUnsafeMutableBufferPointer { UInt(bitPattern: $0.baseAddress) }
        }
        let firstInstanceAddress = storage.withInstanceBytes { bytes -> UInt in
            #expect(bytes.count == 64)
            for index in 0..<bytes.count { #expect(bytes[index] == 0) }
            bytes[0] = 47
            bytes[bytes.count - 1] = 53
            return bytes.withUnsafeMutableBufferPointer { UInt(bitPattern: $0.baseAddress) }
        }
        #expect(firstModelAddress != 0 && firstInstanceAddress != 0)
        let secondModelAddress = storage.withModelBytes { bytes -> UInt in
            #expect(bytes[0] == 41 && bytes[bytes.count - 1] == 43)
            return bytes.withUnsafeMutableBufferPointer { UInt(bitPattern: $0.baseAddress) }
        }
        let secondInstanceAddress = storage.withInstanceBytes { bytes -> UInt in
            #expect(bytes[0] == 47 && bytes[bytes.count - 1] == 53)
            return bytes.withUnsafeMutableBufferPointer { UInt(bitPattern: $0.baseAddress) }
        }
        #expect(firstModelAddress == secondModelAddress)
        #expect(firstInstanceAddress == secondInstanceAddress)
        do {
            try storage.withModelBytes { (_: inout MutableSpan<UInt8>) throws(CallbackError) -> Void in
                throw .stopped
            }
            Issue.record("Model byte callback error was swallowed")
        } catch { #expect(error == .stopped) }
        do {
            try storage.withInstanceBytes { (_: inout MutableSpan<UInt8>) throws(CallbackError) -> Void in
                throw .stopped
            }
            Issue.record("Instance byte callback error was swallowed")
        } catch { #expect(error == .stopped) }
        do {
            try library.close()
            Issue.record("Library closed while local storage was alive")
        } catch {
            guard case .libraryInUse = error else { throw error }
        }
    }

    private func storageWithoutLibrary(_ url: URL) async throws -> OSDIStorage {
        let library = try await load(url)
        return try library.makeStorage(descriptorIndex: 0)
    }

    private func load(_ url: URL, limits requestedLimits: OSDILoadLimits? = nil) async throws -> OSDILibrary {
        let loadLimits = try requestedLimits ?? limits()
        let input = try ArtifactInput(copying: url)
        let result: Result<OSDILibrary, OSDIError>
        do { result = .success(try await input.loader.load(input.intent, source: input.root,
                                                          limits: loadLimits)) }
        catch { result = .failure(error) }
        do { try await input.root.close().wait() }
        catch {
            if case .failure(let primary) = result { throw TestFailure.cleanup(primary, error) }
            throw error
        }
        let library = try result.get()
        #expect(library.reference == input.intent.expectedReference)
        return library
    }

    private func refusedLoad(_ url: URL, limits: OSDILoadLimits? = nil) async throws -> OSDIError {
        do {
            let library = try await load(url, limits: limits)
            try library.close()
            throw TestFailure.expectedRefusal
        } catch let error as OSDIError { return error }
    }

    private func limits() throws -> OSDILoadLimits {
        try OSDILoadLimits(maximumDescriptors: 16, maximumMetadataBytes: 1_048_576,
                           maximumStorageBytes: 1_048_576)
    }

    private func expectReadFailure(_ error: OSDIError) {
        guard case .artifact(.readFailed(let reason)) = error else {
            Issue.record("Read failure was not preserved: \(error)"); return
        }
        #expect(reason == "fixture read failure")
    }

    private func expectCloseFailure(_ error: OSDIError) {
        guard case .artifactClose(.cleanupFailed(let reason)) = error else {
            Issue.record("Close failure was not preserved: \(error)"); return
        }
        #expect(reason == "fixture close failure")
    }

    private enum CallbackError: Error, Equatable { case stopped }
    private enum TestFailure: Error {
        case expectedRefusal
        case unexpectedError(OSDIError)
        case cleanup(any Error, any Error)
    }

    private struct ArtifactInput {
        let file: URL
        let intent: ArtifactAccessIntent
        let root: ArtifactRootCapability
        let loader: OSDINativeLibraryLoader

        init(copying source: URL) throws {
            let directory = FileManager.default.temporaryDirectory
                .appendingPathComponent("swift-osdi-input-\(UUID().uuidString)", isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            file = directory.appendingPathComponent("input.osdi")
            try FileManager.default.copyItem(at: source, to: file)
            let reference = try LocalArtifactReferencer(digester: SHA256ContentDigester()).reference(
                ArtifactLocator(location: ArtifactLocation(workspaceRelativePath: "input.osdi"),
                                role: .input, kind: .model, format: .raw), relativeTo: directory)
            let rootID = try ArtifactRootID(rawValue: UUID().uuidString)
            root = try ArtifactRootCapability(rootID: rootID, directoryURL: directory,
                                               digester: SHA256ContentDigester())
            intent = try ArtifactAccessIntent(expectedReference: reference,
                availability: .local(artifactID: reference.id, rootID: rootID,
                                     relativePath: ArtifactRelativePath(segments: ["input.osdi"])),
                operation: .read,
                budget: ArtifactAccessBudget(maximumPageByteCount: 4096,
                    maximumTotalByteCount: 16_777_216, maximumPageCount: 4096,
                    maximumWorkUnitCount: 8192, maximumDurationNanoseconds: 5_000_000_000))
            loader = OSDINativeLibraryLoader(stagingParentDirectory: directory)
        }
    }

    private actor LeaseBarrier {
        private var leaseReady = false
        private var released = false
        private var leaseWaiters: [CheckedContinuation<Void, Never>] = []
        private var releaseWaiters: [CheckedContinuation<Void, Never>] = []

        func announceLease() {
            leaseReady = true
            let waiters = leaseWaiters
            leaseWaiters.removeAll()
            for waiter in waiters { waiter.resume() }
        }
        func waitForLease() async {
            if leaseReady { return }
            await withCheckedContinuation { leaseWaiters.append($0) }
        }
        func waitForRelease() async {
            if released { return }
            await withCheckedContinuation { releaseWaiters.append($0) }
        }
        func release() {
            released = true
            let waiters = releaseWaiters
            releaseWaiters.removeAll()
            for waiter in waiters { waiter.resume() }
        }
    }

    private struct FaultSource: ArtifactAccessing {
        let base: any ArtifactAccessing
        let readFailure: Bool
        let closeFailure: Bool
        func open(_ intent: ArtifactAccessIntent) async throws(ArtifactAccessError) -> any ArtifactReadSession {
            FaultSession(base: try await base.open(intent), readFailure: readFailure, closeFailure: closeFailure)
        }
    }

    private final class FaultSession: ArtifactReadSession {
        let base: any ArtifactReadSession
        let readFailure: Bool
        let closeFailure: Bool
        var identity: ArtifactAccessSessionIdentity { base.identity }
        var expectedReference: ArtifactReference { base.expectedReference }
        init(base: any ArtifactReadSession, readFailure: Bool, closeFailure: Bool) {
            self.base = base; self.readFailure = readFailure; self.closeFailure = closeFailure
        }
        func readPage(_ request: ArtifactReadPageRequest) async throws(ArtifactAccessError) -> ArtifactReadPage {
            if readFailure { throw .readFailed(reason: "fixture read failure") }
            return try await base.readPage(request)
        }
        func close() async -> any ArtifactAccessTermination {
            let termination = await base.close()
            return closeFailure ? FaultTermination(base: termination) : termination
        }
    }

    private final class FaultTermination: ArtifactAccessTermination {
        let base: any ArtifactAccessTermination
        var sessionIdentity: ArtifactAccessSessionIdentity { base.sessionIdentity }
        init(base: any ArtifactAccessTermination) { self.base = base }
        func wait() async throws(ArtifactAccessTerminationError) -> ArtifactAccessTerminationReceipt {
            _ = try await base.wait()
            throw .cleanupFailed(reason: "fixture close failure")
        }
    }
}
