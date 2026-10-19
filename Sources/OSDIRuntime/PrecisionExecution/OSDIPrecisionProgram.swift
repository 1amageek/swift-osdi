import CircuiteFoundation
import OSDIContracts

/// Immutable cold snapshot for independent replay; no model pointer survives extraction.
public struct OSDIPrecisionProgram: Sendable {
    public let modelReference: ArtifactReference
    public let descriptorIndex: UInt32
    public let topology: OSDIPreparedTopology
    private let legacyProgram: [UInt8]
    private let legacySetup: [UInt8]
    private let controlledBacking: OSDIPrecisionSnapshotBacking?
    /// Controlled snapshots return a caller-owned copy; admit that copy separately.
    public var programBytes: [UInt8] {
        if let controlledBacking { return controlledBacking.program.withBytes { Array($0) } }
        return legacyProgram
    }
    /// Controlled snapshots return a caller-owned copy; admit that copy separately.
    public var setupBytes: [UInt8] {
        if let controlledBacking { return controlledBacking.setup.withBytes { Array($0) } }
        return legacySetup
    }
    /// Stored byte count; does not extract or allocate a public Array.
    public var programByteCount: Int { controlledBacking?.program.count ?? legacyProgram.count }
    /// Stored byte count; does not extract or allocate a public Array.
    public var setupByteCount: Int { controlledBacking?.setup.count ?? legacySetup.count }
    internal func withProgramBytes<Result>(_ body: (UnsafeBufferPointer<UInt8>) throws -> Result) rethrows -> Result {
        if let controlledBacking { return try withExtendedLifetime(controlledBacking) { try controlledBacking.program.withBytes { try body($0.bindMemory(to: UInt8.self)) } } }
        return try legacyProgram.withUnsafeBufferPointer(body)
    }
    internal func withSetupBytes<Result>(_ body: (UnsafeBufferPointer<UInt8>) throws -> Result) rethrows -> Result {
        if let controlledBacking { return try withExtendedLifetime(controlledBacking) { try controlledBacking.setup.withBytes { try body($0.bindMemory(to: UInt8.self)) } } }
        return try legacySetup.withUnsafeBufferPointer(body)
    }
    public let programDigest: ContentDigest
    public let setupDigest: ContentDigest
    public let semanticProfile: ContentDigest
    internal init(modelReference: ArtifactReference, descriptorIndex: UInt32, topology: OSDIPreparedTopology,
                  programBytes: [UInt8], setupBytes: [UInt8], programDigest: ContentDigest,
                  setupDigest: ContentDigest, semanticProfile: ContentDigest) {
        self.modelReference = modelReference; self.descriptorIndex = descriptorIndex; self.topology = topology
        legacyProgram = programBytes; legacySetup = setupBytes
        controlledBacking = nil
        self.programDigest = programDigest
        self.setupDigest = setupDigest; self.semanticProfile = semanticProfile
    }
    internal init(modelReference: ArtifactReference, descriptorIndex: UInt32, topology: OSDIPreparedTopology,
        program: OSDIPrecisionSnapshotBuffer, setup: OSDIPrecisionSnapshotBuffer,
        programDigest: ContentDigest, setupDigest: ContentDigest, semanticProfile: ContentDigest,
        reservation: any Sendable) {
        self.modelReference = modelReference; self.descriptorIndex = descriptorIndex; self.topology = topology
        legacyProgram = []; legacySetup = []; controlledBacking = OSDIPrecisionSnapshotBacking(program: program, setup: setup, reservation: reservation)
        self.programDigest = programDigest; self.setupDigest = setupDigest; self.semanticProfile = semanticProfile
    }
}
