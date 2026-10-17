import COSDI
import OSDIContracts

internal struct OSDIMetadataSnapshot {
    let topology: OSDIPreparedTopology
    let parameters: [OSDIParameterInfo]
    let resolved: [OSDIResolvedParameter]
    let diagnostics: [OSDIDiagnostic]

    init(library: OpaquePointer, evaluator: OpaquePointer, descriptor: OSDIDescriptorInfo,
         limits: OSDIEvaluationLimits) throws(OSDIError) {
        var info = cs_osdi_evaluator_info_record(), error = cs_osdi_error()
        guard cs_osdi_evaluator_info(evaluator, &info, &error) == 0 else { throw .native(NativeIssueBridge.issue(error)) }
        guard Int(info.node_count) == descriptor.nodeCount, Int(info.state_count) == descriptor.stateCount,
              info.active_unknown_count <= info.node_count, info.node_count == 0 || info.collapse_map != nil,
              info.diagnostic_count <= limits.maximumDiagnosticCount,
              info.diagnostic_count == 0 || info.diagnostics != nil else { throw .invalidRange }
        var nodes: [OSDINodeInfo] = []; nodes.reserveCapacity(descriptor.nodeCount)
        for index in 0..<descriptor.nodeCount {
            var node = cs_osdi_node_info_record()
            guard cs_osdi_node_info(library, UInt32(descriptor.index), UInt32(index), &node, &error) == 0 else {
                throw .native(NativeIssueBridge.issue(error))
            }
            let active = info.collapse_map![index]
            do {
                nodes.append(try OSDINodeInfo(index: index,
                    name: OSDINativeText.copy(node.name, maximumBytes: limits.maximumStringBytes),
                    units: OSDINativeText.copy(node.units, maximumBytes: limits.maximumStringBytes),
                    residualUnits: OSDINativeText.copy(node.residual_units, maximumBytes: limits.maximumStringBytes),
                    isFlow: node.is_flow != 0,
                    unknownNature: OSDINatureReference(kind: node.unknown_nature_kind, index: node.unknown_nature_index),
                    residualNature: OSDINatureReference(kind: node.residual_nature_kind, index: node.residual_nature_index),
                    activeUnknownIndex: active == UInt32.max ? nil : Int(active)))
            } catch let failure as OSDIError { throw failure }
            catch let failure as OSDIContractError { throw .contract(failure) }
            catch { throw .host(operation: "copy node metadata", underlying: error) }
        }
        do { topology = try OSDIPreparedTopology(nodes: nodes, activeUnknownCount: Int(info.active_unknown_count), stateCount: Int(info.state_count)) }
        catch { throw .contract(error) }
        let (parameterCount, overflow) = descriptor.parameterCount.addingReportingOverflow(descriptor.operatingPointCount)
        guard !overflow else { throw .resourceLimit }
        var metadata: [OSDIParameterInfo] = []; metadata.reserveCapacity(parameterCount)
        var values: [OSDIResolvedParameter] = []
        for index in 0..<parameterCount {
            var parameter = cs_osdi_parameter_info_record()
            guard cs_osdi_parameter_info(library, UInt32(descriptor.index), UInt32(index), &parameter, &error) == 0 else {
                throw .native(NativeIssueBridge.issue(error))
            }
            guard let rawKind = Int32(exactly: parameter.kind), let kind = OSDIParameterKind(rawValue: rawKind),
                  let rawType = Int32(exactly: parameter.value_type), let type = OSDIParameterType(rawValue: rawType) else { throw .invalidRange }
            var names = [try OSDINativeText.copy(parameter.name, maximumBytes: limits.maximumStringBytes)]
            for alias in 0..<parameter.alias_count {
                var name = cs_osdi_string_view()
                guard cs_osdi_parameter_alias(library, UInt32(descriptor.index), UInt32(index), alias, &name, &error) == 0 else {
                    throw .native(NativeIssueBridge.issue(error))
                }
                names.append(try OSDINativeText.copy(name, maximumBytes: limits.maximumStringBytes))
            }
            let fact: OSDIParameterInfo
            do {
                fact = try OSDIParameterInfo(index: index, names: names, kind: kind, type: type,
                    arrayLength: Int(parameter.array_length),
                    description: OSDINativeText.copy(parameter.description, maximumBytes: limits.maximumStringBytes),
                    units: OSDINativeText.copy(parameter.units, maximumBytes: limits.maximumStringBytes))
            } catch let failure as OSDIError { throw failure }
            catch let failure as OSDIContractError { throw .contract(failure) }
            catch { throw .host(operation: "copy parameter metadata", underlying: error) }
            metadata.append(fact)
            if index < descriptor.parameterCount, fact.arrayLength == 0, kind != .operatingPoint {
                let scopes: [OSDIParameterScope] = kind == .instance ? [.model, .instance] : [.model]
                for scope in scopes {
                    var value = cs_osdi_value(), modelGiven: UInt32 = 0, instanceGiven: UInt32 = 0
                    guard cs_osdi_evaluator_parameter(evaluator, UInt32(index), UInt32(scope.rawValue), &value,
                        &modelGiven, &instanceGiven, &error) == 0 else { throw .native(NativeIssueBridge.issue(error)) }
                    let resolvedValue: OSDIParameterValue
                    switch value.value_type {
                    case 0: resolvedValue = .real(value.real_value)
                    case 1: resolvedValue = .integer(value.integer_value)
                    case 2: resolvedValue = .string(try OSDINativeText.copy(value.string_value, maximumBytes: limits.maximumStringBytes))
                    default: throw .invalidRange
                    }
                    do {
                        values.append(try OSDIResolvedParameter(parameter: fact, scope: scope, value: resolvedValue,
                            isGiven: scope == .model ? modelGiven != 0 : instanceGiven != 0))
                    } catch { throw .contract(error) }
                }
            }
        }
        parameters = metadata; resolved = values
        var copiedDiagnostics: [OSDIDiagnostic] = []; copiedDiagnostics.reserveCapacity(Int(info.diagnostic_count))
        for index in 0..<Int(info.diagnostic_count) {
            let diagnostic = info.diagnostics![index]
            copiedDiagnostics.append(OSDIDiagnostic(level: diagnostic.level,
                message: try OSDINativeText.copy(diagnostic.message, maximumBytes: limits.maximumDiagnosticBytes)))
        }
        diagnostics = copiedDiagnostics
    }
}
