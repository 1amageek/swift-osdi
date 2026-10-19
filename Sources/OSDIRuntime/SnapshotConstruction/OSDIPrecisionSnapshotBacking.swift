/// Immutable shared retention authority for every controlled program/session copy.
internal final class OSDIPrecisionSnapshotBacking: Sendable {
    let program: OSDIPrecisionSnapshotBuffer
    let setup: OSDIPrecisionSnapshotBuffer
    private let metadataReservation: any Sendable
    init(program: OSDIPrecisionSnapshotBuffer, setup: OSDIPrecisionSnapshotBuffer, reservation: any Sendable) {
        self.program = program; self.setup = setup; metadataReservation = reservation
    }
}
