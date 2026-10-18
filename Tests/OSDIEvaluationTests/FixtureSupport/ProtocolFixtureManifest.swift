import Foundation

struct ProtocolFixtureManifest: Decodable, Sendable {
    struct Worker: Decodable, Sendable {
        let path: String
        let sha256: String
    }
    let schema: String
    let protocolLibraries: [String: EvaluationFixtureManifest.ProtocolLibrary]
    let transportWorker: Worker
}
