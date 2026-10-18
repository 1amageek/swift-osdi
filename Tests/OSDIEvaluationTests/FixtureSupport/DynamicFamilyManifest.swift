import Foundation

struct DynamicFamilyManifest: Decodable {
    struct Library: Decodable { let id: String; let path: String; let sha256: String }
    struct Case: Decodable {
        struct Input: Decodable {
            struct Device: Decodable {
                let library: String
                let descriptorIndex: Int
                let temperatureKelvin: Double
                let connectedTerminalCount: Int
                let parameters: [EvaluationFixtureRequest.Parameter]
                let previousState: [Double]
            }
            let devices: [Device]
        }
        struct Reference: Decodable {
            struct Unknown: Decodable { let id: String; let value: Double }
            struct Device: Decodable {
                struct Node: Decodable { let index: Int; let name: String; let unknownID: String? }
                let rawNodes: [Node]
            }
            let unknowns: [Unknown]?
            let devices: [Device]?
        }
        let id: String
        let input: Input
        let reference: Reference
    }
    struct Observation: Decodable {
        struct NoiseSource: Decodable {
            let name: String
            let kind: UInt32?
            let positiveNodeIndex: Int
            let negativeNodeIndex: Int?
        }
        let nodes: [String]
        let collapseMap: [Int?]
        let stateCount: Int
        let localUnknowns: [Double]
        let f: [Double]
        let q: [Double]
        let g: [Double]
        let c: [Double]
        let noiseDensities: [Double]
        let noiseSources: [NoiseSource]
        let nextState: [Double]
        let modelFlags: UInt32
        let maximumTimestep: Double?
        let parameters: [EvaluationReference.Parameter]
    }
    let libraries: [Library]
    let cases: [Case]
}
