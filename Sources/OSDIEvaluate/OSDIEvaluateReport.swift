import OSDIContracts
import OSDIRuntime

struct OSDIEvaluateReport: Codable {
    struct Binding: Codable {
        let sha256: String
        let byteCount: UInt64
    }
    struct Descriptor: Codable {
        let index: Int
        let name: String
        let descriptorStride: UInt64
        let nodeCount: Int
        let terminalCount: Int
        let jacobianEntryCount: Int
        let stateCount: Int

        init(_ value: OSDIDescriptorInfo, stride: UInt64) {
            index = value.index; name = value.name; descriptorStride = stride
            nodeCount = value.nodeCount; terminalCount = value.terminalCount
            jacobianEntryCount = value.jacobianEntryCount; stateCount = value.stateCount
        }
    }
    struct Nature: Codable {
        let kind: UInt32
        let index: UInt32
        init(_ value: OSDINatureReference) { kind = value.kind; index = value.index }
    }
    struct Node: Codable {
        let index: Int
        let name: String
        let units: String
        let residualUnits: String
        let isFlow: Bool
        let unknownNature: Nature
        let residualNature: Nature
        let activeUnknownIndex: Int?

        init(_ value: OSDINodeInfo) {
            index = value.index; name = value.name; units = value.units
            residualUnits = value.residualUnits; isFlow = value.isFlow
            unknownNature = Nature(value.unknownNature); residualNature = Nature(value.residualNature)
            activeUnknownIndex = value.activeUnknownIndex
        }
    }
    struct Topology: Codable {
        let nodes: [Node]
        let activeUnknownCount: Int
        let stateCount: Int
        init(_ value: OSDIPreparedTopology) {
            nodes = value.nodes.map(Node.init)
            activeUnknownCount = value.activeUnknownCount; stateCount = value.stateCount
        }
    }
    struct ParameterInfo: Codable {
        let index: Int
        let names: [String]
        let description: String
        let units: String
        let kind: String
        let type: String
        let arrayLength: Int

        init(_ value: OSDIParameterInfo) {
            index = value.index; names = value.names; description = value.description; units = value.units
            switch value.kind {
            case .model: kind = "model"
            case .instance: kind = "instance"
            case .operatingPoint: kind = "operatingPoint"
            }
            switch value.type {
            case .real: type = "real"
            case .integer: type = "integer"
            case .string: type = "string"
            }
            arrayLength = value.arrayLength
        }
    }
    struct ResolvedParameter: Codable {
        let parameter: ParameterInfo
        let scope: String
        let value: OSDIEvaluateRequest.Value
        let isGiven: Bool
        init(_ value: OSDIResolvedParameter) {
            parameter = ParameterInfo(value.parameter)
            scope = value.scope == .model ? "model" : "instance"
            self.value = OSDIEvaluateRequest.Value(value.value)
            isGiven = value.isGiven
        }
    }
    struct Diagnostic: Codable {
        let level: UInt32
        let message: String
        init(_ value: OSDIDiagnostic) { level = value.level; message = value.message }
    }
    struct NoiseSource: Codable {
        let index: Int
        let name: String
        let kind: UInt32
        let positiveNodeIndex: Int
        let negativeNodeIndex: Int?
        init(_ value: OSDINoiseSourceInfo) {
            index = value.index; name = value.name; kind = value.kind.rawValue
            positiveNodeIndex = value.positiveNodeIndex; negativeNodeIndex = value.negativeNodeIndex
        }
    }
    struct Evaluation: Codable {
        let f: [Double]
        let q: [Double]
        let g: [Double]
        let c: [Double]
        let nextState: [Double]
        let noiseDensities: [Double]?
        let maximumTimestep: Double?
        let modelFlags: UInt32
        let diagnostics: [Diagnostic]
    }
    struct Prepared: Codable {
        let noiseSources: [NoiseSource]?
        let topology: Topology
        let parameterMetadata: [ParameterInfo]
        let resolvedParameters: [ResolvedParameter]
        let diagnostics: [Diagnostic]
    }
    struct Failure: Codable {
        let reason: String
        let detail: String
        let nativeStatus: Int32?
        let descriptorIndex: UInt32?
        let field: String?
        let modelFlags: UInt32?
        let setupErrorCount: UInt32?
        let setupErrorCode: UInt32?
        let setupParameterID: UInt32?
    }

    let schemaVersion: Int
    let toolVersion: String
    let osdiABIVersion: String
    let stage: String
    let succeeded: Bool
    let requestBinding: Binding?
    let input: OSDIEvaluateRequest?
    let artifactBinding: Binding?
    let descriptor: Descriptor?
    let prepared: Prepared?
    let evaluation: Evaluation?
    let failure: Failure?

    init(succeeded: Bool, requestBinding: Binding?, input: OSDIEvaluateRequest?,
         descriptor: Descriptor? = nil, prepared: Prepared? = nil,
         evaluation: Evaluation? = nil, failure: Failure? = nil) {
        schemaVersion = input?.schemaVersion ?? 1; toolVersion = schemaVersion == 1 ? "swift-osdi/S2-schema1" : "swift-osdi/S4-schema2"; osdiABIVersion = "0.4"
        stage = input?.operation == "prepare" ? "osdi-preparation" : "osdi-point-evaluation"
        self.succeeded = succeeded
        self.requestBinding = requestBinding; self.input = input
        artifactBinding = input.map { Binding(sha256: $0.expectedSHA256, byteCount: $0.expectedByteCount) }
        self.descriptor = descriptor; self.prepared = prepared
        self.evaluation = evaluation; self.failure = failure
    }
}
