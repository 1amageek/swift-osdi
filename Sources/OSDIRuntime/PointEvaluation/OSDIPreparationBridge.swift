import COSDI
import OSDIContracts

/// Cold lexical C tables; every string pointer is derived inside the single buffer borrow.
internal struct OSDIPreparationBridge {
    private struct Location {
        let nameOffset: Int
        let nameLength: Int
        let stringOffset: Int?
    }
    private let options: OSDIPreparationOptions
    private let bytes: [CChar]
    private let parameters: [Location]
    private let simulation: [Location]
    private let connectedTerminals: UInt32

    init(options: borrowing OSDIPreparationOptions) throws(OSDIError) {
        guard let connected = UInt32(exactly: options.connectedTerminalCount) else { throw .invalidRange }
        let (parameterBytes, overflow1) = UInt64(options.parameters.count).multipliedReportingOverflow(by: UInt64(MemoryLayout<cs_osdi_parameter_assignment>.stride))
        let (simulationBytes, overflow2) = UInt64(options.simulationParameters.count).multipliedReportingOverflow(by: UInt64(MemoryLayout<cs_osdi_simulation_parameter>.stride))
        let (tableBytes, overflow3) = parameterBytes.addingReportingOverflow(simulationBytes)
        guard !overflow1, !overflow2, !overflow3, tableBytes <= options.limits.maximumWorkspaceBytes else { throw .resourceLimit }
        var size: UInt64 = 0
        func count(_ text: String) throws(OSDIError) {
            let (width, overflow) = UInt64(text.utf8.count).addingReportingOverflow(1)
            let (next, totalOverflow) = size.addingReportingOverflow(width)
            guard !overflow, !totalOverflow, next <= options.limits.maximumStringBytes,
                  next <= UInt64(Int.max), next <= options.limits.maximumWorkspaceBytes - tableBytes else { throw .resourceLimit }
            size = next
        }
        for assignment in options.parameters {
            try count(assignment.name)
            if case .string(let value) = assignment.value { try count(value) }
        }
        for parameter in options.simulationParameters {
            try count(parameter.name)
            if case .string(let value) = parameter.value { try count(value) }
        }
        var buffer: [CChar] = []; buffer.reserveCapacity(Int(size))
        func append(_ text: String) -> Int {
            let offset = buffer.count
            for byte in text.utf8 { buffer.append(CChar(bitPattern: byte)) }
            buffer.append(0)
            return offset
        }
        var parameterLocations: [Location] = []; parameterLocations.reserveCapacity(options.parameters.count)
        for assignment in options.parameters {
            let offset = append(assignment.name)
            let stringOffset: Int?
            if case .string(let value) = assignment.value { stringOffset = append(value) } else { stringOffset = nil }
            parameterLocations.append(Location(nameOffset: offset, nameLength: assignment.name.utf8.count, stringOffset: stringOffset))
        }
        var simulationLocations: [Location] = []; simulationLocations.reserveCapacity(options.simulationParameters.count)
        for parameter in options.simulationParameters {
            let offset = append(parameter.name)
            let stringOffset: Int?
            if case .string(let value) = parameter.value { stringOffset = append(value) } else { stringOffset = nil }
            simulationLocations.append(Location(nameOffset: offset, nameLength: parameter.name.utf8.count, stringOffset: stringOffset))
        }
        self.options = copy options; bytes = buffer
        parameters = parameterLocations; simulation = simulationLocations; connectedTerminals = connected
    }

    func withOptions<Result>(_ body: (UnsafePointer<cs_osdi_prepare_options>) -> Result) -> Result {
        bytes.withUnsafeBufferPointer { strings in
            var assignments: [cs_osdi_parameter_assignment] = []; assignments.reserveCapacity(parameters.count)
            for (index, location) in parameters.enumerated() {
                var assignment = cs_osdi_parameter_assignment()
                assignment.name = cs_osdi_string_view(data: strings.baseAddress!.advanced(by: location.nameOffset), length: UInt64(location.nameLength))
                assignment.scope = UInt32(options.parameters[index].scope.rawValue)
                assignment.value = nativeValue(options.parameters[index].value, offset: location.stringOffset, strings: strings)
                assignments.append(assignment)
            }
            var simulationValues: [cs_osdi_simulation_parameter] = []; simulationValues.reserveCapacity(simulation.count)
            for (index, location) in simulation.enumerated() {
                var parameter = cs_osdi_simulation_parameter()
                parameter.name = cs_osdi_string_view(data: strings.baseAddress!.advanced(by: location.nameOffset), length: UInt64(location.nameLength))
                parameter.value = nativeValue(options.simulationParameters[index].value, offset: location.stringOffset, strings: strings)
                simulationValues.append(parameter)
            }
            return assignments.withUnsafeBufferPointer { assignmentBuffer in
                simulationValues.withUnsafeBufferPointer { simulationBuffer in
                    var native = cs_osdi_prepare_options()
                    native.temperature_kelvin = options.temperatureKelvin
                    native.connected_terminal_count = connectedTerminals
                    native.parameters = assignmentBuffer.baseAddress; native.parameter_count = UInt64(assignments.count)
                    native.simulation_parameters = simulationBuffer.baseAddress; native.simulation_parameter_count = UInt64(simulationValues.count)
                    native.limits = cs_osdi_evaluation_limits(maximum_workspace_bytes: options.limits.maximumWorkspaceBytes,
                        maximum_diagnostic_count: options.limits.maximumDiagnosticCount,
                        maximum_diagnostic_bytes: options.limits.maximumDiagnosticBytes,
                        maximum_string_bytes: options.limits.maximumStringBytes)
                    return withUnsafePointer(to: &native, body)
                }
            }
        }
    }

    private func nativeValue(_ value: OSDIParameterValue, offset: Int?, strings: UnsafeBufferPointer<CChar>) -> cs_osdi_value {
        var native = cs_osdi_value(); native.value_type = UInt32(value.type.rawValue)
        switch value {
        case .real(let number): native.real_value = number
        case .integer(let number): native.integer_value = number
        case .string(let text):
            native.string_value = cs_osdi_string_view(data: strings.baseAddress!.advanced(by: offset!), length: UInt64(text.utf8.count))
        }
        return native
    }
}
