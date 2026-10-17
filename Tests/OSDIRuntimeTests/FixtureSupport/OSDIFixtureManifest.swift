import Foundation

struct OSDIFixtureManifest: Decodable, Sendable {
    struct Library: Decodable, Sendable {
        let path: String
        let sha256: String
        let lifetimeMarker: String
        let kind: Int
    }

    struct Model: Decodable, Sendable {
        let path: String
        let sha256: String
        let sourceSHA256: String
    }

    let schemaVersion: Int
    let libraries: [String: Library]
    let models: [String: Model]
}
