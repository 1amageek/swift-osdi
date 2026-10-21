import Foundation

actor ProtocolFixtureCache {
    static let shared = ProtocolFixtureCache()
    private var preparation: Task<ProtocolFixtureManifest, any Error>?

    func fixtures() async throws -> ProtocolFixtureManifest {
        if let preparation { return try await preparation.value }
        let task = Task { try await Self.buildFresh() }
        preparation = task
        return try await task.value
    }

    private static func buildFresh() async throws -> ProtocolFixtureManifest {
        let root = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        if let path = ProcessInfo.processInfo.environment["OSDI_STANDALONE_FIXTURES"] {
            return try decode(URL(fileURLWithPath: path))
        }
        return try await ProtocolFixturePreparation.prepare(root: root)
    }

    private static func decode(_ path: URL) throws -> ProtocolFixtureManifest {
        let manifest = try JSONDecoder().decode(ProtocolFixtureManifest.self, from: Data(contentsOf: path))
        guard manifest.schema == "osdi.standalone-fixtures.v1", manifest.protocolLibraries.count == 15 else {
            throw EvaluationFixtureError.invalidReference("invalid standalone fixture manifest")
        }
        return manifest
    }
}
