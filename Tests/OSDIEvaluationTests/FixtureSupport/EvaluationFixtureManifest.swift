import Foundation

struct EvaluationFixtureManifest: Decodable, Sendable {
    struct Case: Decodable, Sendable {
        let id: String
        let library: String
        let artifactSHA256: String
        let input: EvaluationFixtureRequest
        let reference: EvaluationReference
    }
    struct ProtocolLibrary: Decodable, Sendable {
        let path: String
        let sha256: String
        let mode: Int
    }
    struct Discriminator: Decodable, Sendable {
        let reproduced: Bool
        let defaultProfile: String
        let qualifiedProfile: String
    }
    let compilerProfile: String
    let defaultProfileDiscriminator: Discriminator
    let protocolLibraries: [String: ProtocolLibrary]
    let schema: String
    let bsimSHA256: String
    let finiteDifferenceChecks: Int
    let finiteDifferenceStatus: String
    let cases: [Case]

    func point(_ id: String) throws -> Case {
        guard let point = cases.first(where: { $0.id == id }) else {
            throw EvaluationFixtureError.invalidReference("missing case \(id)")
        }
        return point
    }
}
