/// Caller-owned cumulative construction work, cancellation and managed-payload admission.
public protocol OSDIPrecisionSnapshotControlling: Sendable {
    /// Admit work before execution; zero work still checks cancellation.
    func checkpoint(workUnits: UInt64) throws
    /// Admit initialized payload before allocation and retain the receipt while it is live.
    func reserveStorage(byteCount: UInt64) throws -> any Sendable
}
