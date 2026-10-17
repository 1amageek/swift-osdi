import COSDI
import CircuiteFoundation
import CircuiteFoundationCrypto
import OSDIContracts

/// Preparation-only serialization. The source program owns defaults; only explicit facts are copied.
internal enum OSDIPrecisionProgramBuilder {
    static func digest(_ bytes: [UInt8]) throws(OSDIError) -> ContentDigest {
        do {
            return try SHA256ContentDigester().digest(using: .sha256, limits: .singleBufferMaximum) { lease throws(ContentDigestError) in
                try lease.update(bytes)
            }.digest
        } catch { throw .host(operation: "digest precision bytes", underlying: error) }
    }
    static func digestBytes(_ digest: ContentDigest) throws(OSDIError) -> [UInt8] {
        guard digest.algorithm == .sha256 else { throw .unsupportedDigest }
        let text = Array(digest.hexadecimalValue.utf8)
        guard text.count == 64 else { throw .identityMismatch }
        func nibble(_ v: UInt8) -> UInt8 { v <= 57 ? v - 48 : v - 87 }
        var result: [UInt8] = []; result.reserveCapacity(32)
        for i in stride(from: 0, to: 64, by: 2) { result.append(nibble(text[i]) * 16 + nibble(text[i + 1])) }
        return result
    }
    static func build(library: borrowing OSDILibrary, descriptorIndex: Int,
        topology: OSDIPreparedTopology, metadata: [OSDIParameterInfo], preparation: OSDIPreparationOptions,
        semanticProfile: ContentDigest, options: OSDIPrecisionOptions) throws(OSDIError) -> OSDIPrecisionProgram {
        guard library.descriptors.indices.contains(descriptorIndex), let index = UInt32(exactly: descriptorIndex),
              topology.stateCount == 0, !topology.nodes.isEmpty,
              topology.nodes.count <= Int(UInt32.max), topology.activeUnknownCount <= Int(UInt32.max),
              preparation.connectedTerminalCount <= Int(UInt32.max) else { throw .contract(.unsupportedEvaluationContext) }
        let lease = try library.owner.acquireLease(descriptor: library.descriptors[descriptorIndex])
        let bytes = try lease.withNativeLibrary { handle throws(OSDIError) -> [UInt8] in
            var pointer: UnsafePointer<UInt8>?, count: UInt64 = 0, issue = cs_osdi_error()
            let status = cs_osdi_precision_program_view(handle, index, options.maximumProgramBytes, &pointer, &count, &issue)
            guard status == 0 else { throw .native(NativeIssueBridge.issue(issue)) }
            guard let pointer, count <= UInt64(Int.max) else { throw .invalidRange }
            return Array(UnsafeBufferPointer(start: pointer, count: Int(count)))
        }
        let header: CSOsdiPrecisionProgramHeader = try load(bytes, offset: 0)
        let magic = withUnsafeBytes(of: header.magic) { Array($0) }
        guard magic == [79,83,68,73,80,82,49,0], header.version == 1,
              header.header_bytes == MemoryLayout<CSOsdiPrecisionProgramHeader>.size,
              header.program_bytes == bytes.count, header.descriptor_index == index,
              header.raw_node_count == topology.nodes.count, header.native_state_count == 0, header.flags == 1,
              withUnsafeBytes(of: header.semantic_profile_sha256, { Array($0) }) == (try digestBytes(semanticProfile)) else { throw .identityMismatch }
        let sections: [(CSOsdiPrecisionSection, Int)] = [
            (header.registers, MemoryLayout<CSOsdiPrecisionRegister>.size), (header.constants, MemoryLayout<CSOsdiPrecisionConstant>.size),
            (header.inputs, MemoryLayout<CSOsdiPrecisionInput>.size), (header.functions, MemoryLayout<CSOsdiPrecisionFunction>.size),
            (header.blocks, MemoryLayout<CSOsdiPrecisionBlock>.size), (header.instructions, MemoryLayout<CSOsdiPrecisionInstruction>.size),
            (header.phi_edges, MemoryLayout<CSOsdiPrecisionPhiEdge>.size), (header.roots, MemoryLayout<CSOsdiPrecisionRoot>.size),
            (header.parameters, MemoryLayout<CSOsdiPrecisionParameter>.size), (header.nodes, MemoryLayout<CSOsdiPrecisionNode>.size), (header.strings, 1)]
        for (section, size) in sections { try validate(section, stride: size, count: bytes.count) }
        guard header.nodes.count == topology.nodes.count else { throw .identityMismatch }
        func text(_ offset: UInt32, _ count: UInt32) throws(OSDIError) -> String {
            guard UInt64(offset) + UInt64(count) <= UInt64(header.strings.count) else { throw .invalidRange }
            let start = Int(header.strings.offset) + Int(offset)
            let slice = bytes[start..<(start + Int(count))]
            guard !slice.contains(0), let value = String(validating: slice, as: UTF8.self) else { throw .invalidRange }
            return value
        }
        for row in topology.nodes.indices {
            let node: CSOsdiPrecisionNode = try load(bytes, offset: Int(header.nodes.offset) + row * MemoryLayout<CSOsdiPrecisionNode>.size)
            guard node.osdi_index == row, try text(node.name_offset, node.name_bytes) == topology.nodes[row].name,
                  (node.is_flow != 0) == topology.nodes[row].isFlow else { throw .identityMismatch }
        }
        var pool: [UInt8] = []
        func appendString(_ text: String) throws(OSDIError) -> (UInt32, UInt32) {
            guard pool.count <= Int(UInt32.max), text.utf8.count <= Int(UInt32.max),
                  UInt64(pool.count) + UInt64(text.utf8.count) <= options.maximumMappingBytes else { throw .resourceLimit }
            let result = (UInt32(pool.count), UInt32(text.utf8.count)); pool.append(contentsOf: text.utf8); return result
        }
        func bits(_ value: OSDIParameterValue) throws(OSDIError) -> UInt64 {
            switch value {
            case .real(let value): return value.bitPattern
            case .integer(let value): return UInt64(bitPattern: Int64(value))
            case .string(let value): let p = try appendString(value); return UInt64(p.0) | UInt64(p.1) << 32
            }
        }
        func type(_ value: OSDIParameterType) -> UInt32 { switch value { case .real: 1; case .integer: 2; case .string: 4 } }
        var explicit: [String: OSDIParameterValue] = [:]
        for assignment in preparation.parameters {
            let matches = metadata.filter { $0.names.contains { $0.lowercased() == assignment.name.lowercased() } }
            guard matches.count == 1, let parameter = matches.first, parameter.arrayLength == 0,
                  parameter.kind != .operatingPoint, parameter.type == assignment.value.type,
                  assignment.scope != .instance || parameter.kind == .instance else { throw .invalidRange }
            let key = "\(parameter.index):\(assignment.scope == .model ? 1 : 2)"
            guard explicit.updateValue(assignment.value, forKey: key) == nil else { throw .invalidRange }
        }
        var facts: [CSOsdiPrecisionParameterFact] = []; var consumed = Set<String>(); var keys = Set<String>()
        for ordinal in 0..<Int(header.parameters.count) {
            let parameter: CSOsdiPrecisionParameter = try load(bytes, offset: Int(header.parameters.offset) + ordinal * MemoryLayout<CSOsdiPrecisionParameter>.size)
            let key = "\(parameter.osdi_index):\(parameter.scope)"
            guard keys.insert(key).inserted, parameter.scope == 1 || parameter.scope == 2,
                  parameter.flags & ~UInt32(CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT) == 0,
                  let native = metadata.first(where: { $0.index == Int(parameter.osdi_index) }),
                  native.names.contains(try text(parameter.name_offset, parameter.name_bytes)),
                  type(native.type) == parameter.type else { throw .identityMismatch }
            let value = explicit[key]
            if value != nil { consumed.insert(key) }
            let valueBits: UInt64
            if let value { valueBits = try bits(value) } else { valueBits = 0 }
            facts.append(CSOsdiPrecisionParameterFact(parameter_index: UInt32(ordinal), given: value == nil ? 0 : 1, bits: valueBits))
        }
        guard consumed.count == explicit.count else { throw .identityMismatch }
        var simulation: [CSOsdiPrecisionSimulationFact] = []
        for fact in preparation.simulationParameters {
            let name = try appendString(fact.name)
            simulation.append(CSOsdiPrecisionSimulationFact(name_offset: name.0, name_bytes: name.1,
                type: type(fact.value.type), given: 1, bits: try bits(fact.value)))
        }
        var setup = [UInt8](repeating: 0, count: MemoryLayout<CSOsdiPrecisionSetupHeader>.size)
        func append<T>(_ values: [T]) throws(OSDIError) -> CSOsdiPrecisionSection {
            let aligned = (setup.count + 7) & ~7
            let (size, overflow) = values.count.multipliedReportingOverflow(by: MemoryLayout<T>.stride)
            guard !overflow, aligned <= Int.max - size, UInt64(aligned + size) <= options.maximumMappingBytes,
                  values.count <= Int(UInt32.max) else { throw .resourceLimit }
            setup.append(contentsOf: repeatElement(0, count: aligned - setup.count))
            let result = CSOsdiPrecisionSection(offset: UInt64(aligned), count: UInt32(values.count), stride: UInt32(MemoryLayout<T>.stride))
            values.withUnsafeBytes { setup.append(contentsOf: $0) }; return result
        }
        var setupHeader = CSOsdiPrecisionSetupHeader()
        setupHeader.version = 1; setupHeader.header_bytes = UInt32(MemoryLayout<CSOsdiPrecisionSetupHeader>.size)
        setupHeader.temperature_binary64 = preparation.temperatureKelvin.bitPattern
        setupHeader.connected_terminal_count = UInt32(preparation.connectedTerminalCount)
        setupHeader.active_unknown_count = UInt32(topology.activeUnknownCount)
        setupHeader.parameter_facts = try append(facts); setupHeader.simulation_facts = try append(simulation)
        setupHeader.raw_to_active = try append(topology.nodes.map { $0.activeUnknownIndex.map(UInt32.init) ?? UInt32.max })
        setupHeader.strings = try append(pool); setupHeader.setup_bytes = UInt64(setup.count)
        withUnsafeBytes(of: setupHeader) { setup.replaceSubrange(0..<$0.count, with: $0) }
        return OSDIPrecisionProgram(modelReference: library.reference, descriptorIndex: index, topology: topology,
            programBytes: bytes, setupBytes: setup, programDigest: try digest(bytes), setupDigest: try digest(setup), semanticProfile: semanticProfile)
    }
    private static func validate(_ section: CSOsdiPrecisionSection, stride: Int, count: Int) throws(OSDIError) {
        let (size, overflow) = UInt64(section.count).multipliedReportingOverflow(by: UInt64(stride))
        guard section.stride == stride, !overflow, section.offset <= count, size <= UInt64(count) - section.offset else { throw .invalidRange }
    }
    private static func load<T>(_ bytes: [UInt8], offset: Int) throws(OSDIError) -> T {
        guard offset >= 0, offset <= bytes.count, MemoryLayout<T>.size <= bytes.count - offset else { throw .invalidRange }
        // Native precision profile is little-endian Apple arm64. Unaligned cold snapshots
        // are copied into value storage; no foreign pointer escapes its image lease.
        return bytes.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: T.self) }
    }
}
