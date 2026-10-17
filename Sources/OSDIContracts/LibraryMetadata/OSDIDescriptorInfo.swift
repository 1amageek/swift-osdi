/// Immutable facts from one admitted descriptor; this is not an initialized model.
public struct OSDIDescriptorInfo: Sendable, Equatable {
    public let index: Int
    public let name: String
    public let nodeCount: Int
    public let terminalCount: Int
    public let jacobianEntryCount: Int
    public let stateCount: Int
    public let collapsibleCount: Int
    public let parameterCount: Int
    public let instanceParameterCount: Int
    public let operatingPointCount: Int
    public let noiseSourceCount: Int
    public let instanceByteCount: Int
    public let modelByteCount: Int

    public init(index: Int, name: String, nodeCount: Int, terminalCount: Int,
                jacobianEntryCount: Int, stateCount: Int, collapsibleCount: Int,
                parameterCount: Int, instanceParameterCount: Int,
                operatingPointCount: Int, noiseSourceCount: Int,
                instanceByteCount: Int, modelByteCount: Int) throws(OSDIContractError) {
        guard index >= 0, !name.isEmpty, nodeCount >= 0, terminalCount >= 0,
              terminalCount <= nodeCount, jacobianEntryCount >= 0, stateCount >= 0,
              collapsibleCount >= 0, parameterCount >= 0, instanceParameterCount >= 0,
              instanceParameterCount <= parameterCount, operatingPointCount >= 0,
              noiseSourceCount >= 0, instanceByteCount >= 0, modelByteCount >= 0 else {
            throw .invalidDescriptor
        }
        self.index = index; self.name = name; self.nodeCount = nodeCount
        self.terminalCount = terminalCount; self.jacobianEntryCount = jacobianEntryCount
        self.stateCount = stateCount; self.collapsibleCount = collapsibleCount
        self.parameterCount = parameterCount; self.instanceParameterCount = instanceParameterCount
        self.operatingPointCount = operatingPointCount; self.noiseSourceCount = noiseSourceCount
        self.instanceByteCount = instanceByteCount; self.modelByteCount = modelByteCount
    }
}
