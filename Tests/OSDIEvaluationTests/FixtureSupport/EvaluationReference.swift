import Foundation
import OSDIContracts

struct EvaluationReference: Decodable, Sendable {
    struct Node: Decodable, Sendable {
        let units: String
        let residualUnits: String
        let isFlow: Bool
    }
    struct Parameter: Decodable, Sendable {
        let index: Int
        let kind: Int32
        let type: Int32
        let names: [String]
        let modelValue: EvaluationParameterValue
        let modelGiven: Bool
        let instanceValue: EvaluationParameterValue?
        let instanceGiven: Bool?
    }
    let descriptor: String
    let evaluationFlags: UInt32
    let modelFlags: UInt32
    let stateCount: Int
    let nodes: [String]
    let nodeMetadata: [Node]
    let collapseMap: [Int?]
    let localUnknowns: [Double]
    let rawPotentials: [Double]
    let f: [Double]
    let q: [Double]
    let g: [Double]
    let c: [Double]
    let nextState: [Double]
    let parameters: [Parameter]
}
