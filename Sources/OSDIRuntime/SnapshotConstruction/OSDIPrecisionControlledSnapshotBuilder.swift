import COSDI
import CircuiteFoundation
import OSDIContracts

/// The controlled ASCII profile plans exact fixed storage before materializing facts.
internal enum OSDIPrecisionControlledSnapshotBuilder {
    private struct AssignmentSlot {
        var metadataIndex: Int
        var scope: UInt32
        var sourceIndex: Int
        var consumed: UInt32
    }
    private static func add(_ a: Int, _ b: Int) throws -> Int {
        let (v, overflow) = a.addingReportingOverflow(b)
        guard !overflow, v >= 0 else { throw OSDIError.resourceLimit }; return v
    }
    private static func multiply(_ a: Int, _ b: Int) throws -> Int {
        let (v, overflow) = a.multipliedReportingOverflow(by: b)
        guard !overflow, v >= 0 else { throw OSDIError.resourceLimit }; return v
    }
    private static func aligned(_ n: Int) throws -> Int { try add(n, 7) & ~7 }
    @discardableResult private static func scan(_ text: String, ascii: Bool = false,
        control: any OSDIPrecisionSnapshotControlling) throws -> Int {
        var count = 0
        for byte in text.utf8 {
            try control.checkpoint(workUnits: 1)
            guard !ascii || byte < 128 else { throw OSDIError.contract(.unsupportedEvaluationContext) }
            count = try add(count, 1)
        }
        return count
    }
    private static func asciiEqual(_ a: String, _ b: String,
        control: any OSDIPrecisionSnapshotControlling) throws -> Bool {
        var lhs = a.utf8.makeIterator(), rhs = b.utf8.makeIterator()
        while true {
            try control.checkpoint(workUnits: 1)
            let x = lhs.next(), y = rhs.next()
            guard let x, let y else { return x == nil && y == nil }
            let fx = (65...90).contains(x) ? x + 32 : x
            let fy = (65...90).contains(y) ? y + 32 : y
            if fx != fy { return false }
        }
    }
    static func validatePreparation(_ requested: OSDIPreparationOptions, matches original: OSDIPreparationOptions,
        control: any OSDIPrecisionSnapshotControlling) throws {
        try control.checkpoint(workUnits: 1)
        guard requested.temperatureKelvin == original.temperatureKelvin,
              requested.connectedTerminalCount == original.connectedTerminalCount,
              requested.limits == original.limits, requested.parameters.count == original.parameters.count,
              requested.simulationParameters.count == original.simulationParameters.count else { throw OSDIError.identityMismatch }
        func exactText(_ a: String, _ b: String, names: Bool) throws -> Bool {
            var lhs = a.utf8.makeIterator(), rhs = b.utf8.makeIterator()
            while true {
                try control.checkpoint(workUnits: 1)
                let x = lhs.next(), y = rhs.next()
                guard let x, let y else { return x == nil && y == nil }
                guard !names || (x < 128 && y < 128) else { throw OSDIError.contract(.unsupportedEvaluationContext) }
                if x != y { return false }
            }
        }
        func value(_ a: OSDIParameterValue, _ b: OSDIParameterValue) throws -> Bool {
            try control.checkpoint(workUnits: 1)
            switch (a, b) {
            case (.real(let x), .real(let y)): return x == y
            case (.integer(let x), .integer(let y)): return x == y
            case (.string(let x), .string(let y)): return try exactText(x, y, names: false)
            default: return false
            }
        }
        for index in requested.parameters.indices {
            try control.checkpoint(workUnits: 1)
            let a = requested.parameters[index], b = original.parameters[index]
            guard a.scope == b.scope, try exactText(a.name, b.name, names: true), try value(a.value, b.value) else { throw OSDIError.identityMismatch }
        }
        for index in requested.simulationParameters.indices {
            try control.checkpoint(workUnits: 1)
            let a = requested.simulationParameters[index], b = original.simulationParameters[index]
            guard try exactText(a.name, b.name, names: true), try value(a.value, b.value) else { throw OSDIError.identityMismatch }
        }
    }
    static func admitPreparationComparison(_ value: OSDIPreparationOptions,
        control: any OSDIPrecisionSnapshotControlling) throws {
        for assignment in value.parameters {
            try control.checkpoint(workUnits: 1); try control.checkpoint(workUnits: UInt64(scan(assignment.name, ascii: true, control: control)))
            if case .string(let text) = assignment.value { try control.checkpoint(workUnits: UInt64(scan(text, control: control))) }
        }
        for fact in value.simulationParameters {
            try control.checkpoint(workUnits: 1); try control.checkpoint(workUnits: UInt64(scan(fact.name, ascii: true, control: control)))
            if case .string(let text) = fact.value { try control.checkpoint(workUnits: UInt64(scan(text, control: control))) }
        }
    }
    static func build(library: borrowing OSDILibrary, descriptorIndex: Int,
        topology: OSDIPreparedTopology, metadata: [OSDIParameterInfo], preparation: OSDIPreparationOptions,
        semanticProfile: ContentDigest, options: OSDIPrecisionOptions,
        control: any OSDIPrecisionSnapshotControlling) throws -> OSDIPrecisionProgram {
        try control.checkpoint(workUnits: 0)
        guard library.descriptors.indices.contains(descriptorIndex), let index = UInt32(exactly: descriptorIndex),
              topology.stateCount == 0, !topology.nodes.isEmpty,
              UInt32(exactly: topology.nodes.count) != nil, UInt32(exactly: topology.activeUnknownCount) != nil,
              UInt32(exactly: preparation.connectedTerminalCount) != nil else { throw OSDIError.contract(.unsupportedEvaluationContext) }
        let lease = try library.owner.acquireLease(descriptor: library.descriptors[descriptorIndex])
        return try lease.withNativeLibrary { handle throws(OSDIError) in
            do {
                // Existing C admission scans at most one export record per descriptor.
                try control.checkpoint(workUnits: UInt64(library.descriptors.count))
                var pointer: UnsafePointer<UInt8>?, count: UInt64 = 0, issue = cs_osdi_error()
                let status = cs_osdi_precision_program_view(handle, index, options.maximumProgramBytes, &pointer, &count, &issue)
                guard status == 0 else { throw OSDIError.native(NativeIssueBridge.issue(issue)) }
                guard let pointer, count <= options.maximumProgramBytes, let size = Int(exactly: count) else { throw OSDIError.resourceLimit }
                let program = try OSDIPrecisionSnapshotBuffer(count: size, control: control)
                try control.checkpoint(workUnits: count)
                program.withMutableBytes { $0.copyMemory(from: UnsafeRawBufferPointer(start: pointer, count: size)) }
                return try construct(program: program, modelReference: library.reference, descriptorIndex: index, topology: topology,
                    metadata: metadata, preparation: preparation, semanticProfile: semanticProfile, options: options, control: control)
            } catch let error as OSDIError { throw error }
            catch { throw .host(operation: "controlled precision snapshot", underlying: error) }
        }
    }
    static func construct(program: OSDIPrecisionSnapshotBuffer, modelReference: ArtifactReference,
        descriptorIndex: UInt32, topology: OSDIPreparedTopology, metadata: [OSDIParameterInfo],
        preparation: OSDIPreparationOptions, semanticProfile: ContentDigest, options: OSDIPrecisionOptions,
        control: any OSDIPrecisionSnapshotControlling) throws -> OSDIPrecisionProgram {
        try admitPreparationComparison(preparation, control: control)
        return try program.withBytes { bytes in
            func load<T>(_ offset: Int, as: T.Type = T.self) throws -> T {
                try control.checkpoint(workUnits: 1)
                guard offset >= 0, offset <= bytes.count, MemoryLayout<T>.size <= bytes.count - offset else { throw OSDIError.invalidRange }
                return bytes.loadUnaligned(fromByteOffset: offset, as: T.self)
            }
            let header: CSOsdiPrecisionProgramHeader = try load(0)
            // Fixed header tuples are compared in place, without temporary Arrays.
            let magicOK = withUnsafeBytes(of: header.magic) { v in
                v.count == 8 && v[0] == 79 && v[1] == 83 && v[2] == 68 && v[3] == 73 && v[4] == 80 && v[5] == 82 && v[6] == 49 && v[7] == 0
            }
            try control.checkpoint(workUnits: 8 + 64)
            guard semanticProfile.algorithm == .sha256 else { throw OSDIError.unsupportedDigest }
            var profileIterator = semanticProfile.hexadecimalValue.utf8.makeIterator()
            let profileOK = withUnsafeBytes(of: header.semantic_profile_sha256) { native in
                for byte in native {
                    guard let high = profileIterator.next(), let low = profileIterator.next() else { return false }
                    func nibble(_ v: UInt8) -> UInt8 { v <= 57 ? v - 48 : v - 87 }
                    guard nibble(high) * 16 + nibble(low) == byte else { return false }
                }
                return profileIterator.next() == nil
            }
            guard magicOK, profileOK, header.version == 1,
                  header.header_bytes == MemoryLayout<CSOsdiPrecisionProgramHeader>.size,
                  header.program_bytes == bytes.count, header.descriptor_index == descriptorIndex,
                  header.raw_node_count == topology.nodes.count, header.native_state_count == 0, header.flags == 1 else { throw OSDIError.identityMismatch }
            func validate(_ section: CSOsdiPrecisionSection, _ stride: Int) throws {
                try control.checkpoint(workUnits: 1)
                let length = try multiply(Int(section.count), stride)
                guard section.stride == stride, section.offset <= bytes.count,
                      length <= bytes.count - Int(section.offset) else { throw OSDIError.invalidRange }
            }
            try validate(header.registers, MemoryLayout<CSOsdiPrecisionRegister>.size)
            try validate(header.constants, MemoryLayout<CSOsdiPrecisionConstant>.size)
            try validate(header.inputs, MemoryLayout<CSOsdiPrecisionInput>.size)
            try validate(header.functions, MemoryLayout<CSOsdiPrecisionFunction>.size)
            try validate(header.blocks, MemoryLayout<CSOsdiPrecisionBlock>.size)
            try validate(header.instructions, MemoryLayout<CSOsdiPrecisionInstruction>.size)
            try validate(header.phi_edges, MemoryLayout<CSOsdiPrecisionPhiEdge>.size)
            try validate(header.roots, MemoryLayout<CSOsdiPrecisionRoot>.size)
            try validate(header.parameters, MemoryLayout<CSOsdiPrecisionParameter>.size)
            try validate(header.nodes, MemoryLayout<CSOsdiPrecisionNode>.size)
            try validate(header.strings, 1)
            guard header.nodes.count == topology.nodes.count else { throw OSDIError.identityMismatch }
            func sourceTextEquals(_ offset: UInt32, _ count: UInt32, _ text: String) throws -> Bool {
                let end = UInt64(offset) + UInt64(count)
                guard end <= UInt64(header.strings.count) else { throw OSDIError.invalidRange }
                var index = Int(header.strings.offset) + Int(offset)
                let stop = index + Int(count)
                for byte in text.utf8 {
                    try control.checkpoint(workUnits: 1)
                    guard index < stop, byte == bytes[index], byte != 0 else { return false }
                    index += 1
                }
                return index == stop
            }
            for row in topology.nodes.indices {
                let node: CSOsdiPrecisionNode = try load(Int(header.nodes.offset) + row * MemoryLayout<CSOsdiPrecisionNode>.size)
                guard node.osdi_index == row, try sourceTextEquals(node.name_offset, node.name_bytes, topology.nodes[row].name),
                      (node.is_flow != 0) == topology.nodes[row].isFlow else { throw OSDIError.identityMismatch }
            }
            for item in metadata {
                try control.checkpoint(workUnits: 1)
                for name in item.names { try scan(name, ascii: true, control: control) }
            }
            // Every assignment must be consumed exactly once. Its STRING bytes therefore
            // determine the final pool before any fact/assignment scratch is allocated.
            var poolCount = 0
            for assignment in preparation.parameters {
                if case .string(let text) = assignment.value { poolCount = try add(poolCount, scan(text, control: control)) }
            }
            for fact in preparation.simulationParameters {
                poolCount = try add(poolCount, scan(fact.name, ascii: true, control: control))
                if case .string(let text) = fact.value { poolCount = try add(poolCount, scan(text, control: control)) }
            }
            let factsOffset = try aligned(MemoryLayout<CSOsdiPrecisionSetupHeader>.size)
            let simulationOffset = try aligned(add(factsOffset, multiply(Int(header.parameters.count), MemoryLayout<CSOsdiPrecisionParameterFact>.stride)))
            let mapOffset = try aligned(add(simulationOffset, multiply(preparation.simulationParameters.count, MemoryLayout<CSOsdiPrecisionSimulationFact>.stride)))
            let stringsOffset = try aligned(add(mapOffset, multiply(topology.nodes.count, MemoryLayout<UInt32>.stride)))
            let setupCount = try add(stringsOffset, poolCount)
            guard UInt64(setupCount) <= options.maximumMappingBytes,
                  UInt32(exactly: poolCount) != nil, UInt32(exactly: preparation.simulationParameters.count) != nil else { throw OSDIError.resourceLimit }
            let slotBytes = try multiply(preparation.parameters.count, MemoryLayout<AssignmentSlot>.stride)
            let slots = try OSDIPrecisionSnapshotBuffer(count: slotBytes, control: control)
            func slot(_ ordinal: Int) -> AssignmentSlot {
                slots.withBytes { $0.loadUnaligned(fromByteOffset: ordinal * MemoryLayout<AssignmentSlot>.stride, as: AssignmentSlot.self) }
            }
            func setSlot(_ value: AssignmentSlot, _ ordinal: Int) {
                slots.withMutableBytes { $0.storeBytes(of: value, toByteOffset: ordinal * MemoryLayout<AssignmentSlot>.stride, as: AssignmentSlot.self) }
            }
            for (ordinal, assignment) in preparation.parameters.enumerated() {
                try control.checkpoint(workUnits: 1)
                var match: Int?, matches = 0
                for (position, item) in metadata.enumerated() {
                    try control.checkpoint(workUnits: 1)
                    var found = false
                    for name in item.names {
                        if try asciiEqual(name, assignment.name, control: control) { found = true; break }
                    }
                    if found { matches += 1; match = position }
                }
                guard matches == 1, let match else { throw OSDIError.invalidRange }
                let item = metadata[match], scope: UInt32 = assignment.scope == .model ? 1 : 2
                guard item.arrayLength == 0, item.kind != .operatingPoint, item.type == assignment.value.type,
                      assignment.scope != .instance || item.kind == .instance else { throw OSDIError.invalidRange }
                for previous in 0..<ordinal {
                    try control.checkpoint(workUnits: 1)
                    let earlier = slot(previous)
                    guard metadata[earlier.metadataIndex].index != item.index || earlier.scope != scope else { throw OSDIError.invalidRange }
                }
                setSlot(AssignmentSlot(metadataIndex: match, scope: scope, sourceIndex: ordinal, consumed: 0), ordinal)
            }
            func parameter(_ ordinal: Int) throws -> CSOsdiPrecisionParameter {
                try load(Int(header.parameters.offset) + ordinal * MemoryLayout<CSOsdiPrecisionParameter>.size)
            }
            func assignmentIndex(_ item: CSOsdiPrecisionParameter) throws -> Int? {
                for ordinal in preparation.parameters.indices {
                    try control.checkpoint(workUnits: 1)
                    let resolved = slot(ordinal)
                    if metadata[resolved.metadataIndex].index == Int(item.osdi_index), resolved.scope == item.scope { return ordinal }
                }
                return nil
            }
            func type(_ value: OSDIParameterType) -> UInt32 {
                switch value { case .real: 1; case .integer: 2; case .string: 4 }
            }
            for ordinal in 0..<Int(header.parameters.count) {
                let item = try parameter(ordinal)
                guard item.scope == 1 || item.scope == 2,
                      item.flags & ~UInt32(CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT) == 0 else { throw OSDIError.identityMismatch }
                for previous in 0..<ordinal {
                    let earlier = try parameter(previous)
                    guard earlier.osdi_index != item.osdi_index || earlier.scope != item.scope else { throw OSDIError.identityMismatch }
                }
                var nativeIndex: Int?
                for position in metadata.indices {
                    try control.checkpoint(workUnits: 1)
                    if metadata[position].index == Int(item.osdi_index) { nativeIndex = position; break }
                }
                guard let nativeIndex, type(metadata[nativeIndex].type) == item.type else { throw OSDIError.identityMismatch }
                var nameMatches = false
                for name in metadata[nativeIndex].names {
                    if try sourceTextEquals(item.name_offset, item.name_bytes, name) { nameMatches = true; break }
                }
                guard nameMatches else { throw OSDIError.identityMismatch }
                if let ordinal = try assignmentIndex(item) {
                    var resolved = slot(ordinal); resolved.consumed = 1; setSlot(resolved, ordinal)
                }
            }
            for ordinal in preparation.parameters.indices {
                try control.checkpoint(workUnits: 1)
                guard slot(ordinal).consumed == 1 else { throw OSDIError.identityMismatch }
            }
            // Retained logical topology/reference/digest payload; shared caller input is never treated as free.
            var retained = try add(MemoryLayout<OSDIPrecisionProgram>.stride, multiply(topology.nodes.count, MemoryLayout<OSDINodeInfo>.stride))
            for node in topology.nodes {
                retained = try add(retained, scan(node.name, control: control))
                retained = try add(retained, scan(node.units, control: control))
                retained = try add(retained, scan(node.residualUnits, control: control))
            }
            retained = try add(retained, 3 * 64)
            retained = try add(retained, scan(modelReference.digest.hexadecimalValue, control: control))
            retained = try add(retained, scan(modelReference.digest.algorithm.rawValue, control: control))
            retained = try add(retained, scan(modelReference.descriptor.role.rawValue, control: control))
            retained = try add(retained, scan(modelReference.descriptor.kind.rawValue, control: control))
            retained = try add(retained, scan(modelReference.descriptor.format.rawValue, control: control))
            let resultReservation = try control.reserveStorage(byteCount: UInt64(retained))
            let setup = try OSDIPrecisionSnapshotBuffer(count: setupCount, control: control)
            try setup.withMutableBytes { output in
                var poolCursor = 0
                func store<T>(_ value: T, at offset: Int) { output.storeBytes(of: value, toByteOffset: offset, as: T.self) }
                func appendText(_ text: String) throws -> (UInt32, UInt32) {
                    let start = poolCursor
                    for byte in text.utf8 {
                        try control.checkpoint(workUnits: 1)
                        guard poolCursor < poolCount else { throw OSDIError.invalidRange }
                        output[stringsOffset + poolCursor] = byte; poolCursor += 1
                    }
                    return (UInt32(start), UInt32(poolCursor - start))
                }
                func bits(_ value: OSDIParameterValue) throws -> UInt64 {
                    switch value {
                    case .real(let v): return v.bitPattern
                    case .integer(let v): return UInt64(bitPattern: Int64(v))
                    case .string(let text): let pair = try appendText(text); return UInt64(pair.0) | UInt64(pair.1) << 32
                    }
                }
                for ordinal in 0..<Int(header.parameters.count) {
                    let item = try parameter(ordinal), assignment = try assignmentIndex(item)
                    let value = assignment.map { preparation.parameters[slot($0).sourceIndex].value }
                    let valueBits = try value.map { try bits($0) } ?? 0
                    store(CSOsdiPrecisionParameterFact(parameter_index: UInt32(ordinal), given: value == nil ? 0 : 1, bits: valueBits),
                        at: factsOffset + ordinal * MemoryLayout<CSOsdiPrecisionParameterFact>.stride)
                }
                for (ordinal, fact) in preparation.simulationParameters.enumerated() {
                    try control.checkpoint(workUnits: 1)
                    let name = try appendText(fact.name)
                    let value = CSOsdiPrecisionSimulationFact(name_offset: name.0, name_bytes: name.1,
                        type: type(fact.value.type), given: 1, bits: try bits(fact.value))
                    store(value, at: simulationOffset + ordinal * MemoryLayout<CSOsdiPrecisionSimulationFact>.stride)
                }
                for (ordinal, node) in topology.nodes.enumerated() {
                    try control.checkpoint(workUnits: 1)
                    store(node.activeUnknownIndex.map(UInt32.init) ?? UInt32.max, at: mapOffset + ordinal * MemoryLayout<UInt32>.stride)
                }
                guard poolCursor == poolCount else { throw OSDIError.identityMismatch }
                var h = CSOsdiPrecisionSetupHeader()
                h.version = 1; h.header_bytes = UInt32(MemoryLayout<CSOsdiPrecisionSetupHeader>.size)
                h.temperature_binary64 = preparation.temperatureKelvin.bitPattern
                h.connected_terminal_count = UInt32(preparation.connectedTerminalCount)
                h.active_unknown_count = UInt32(topology.activeUnknownCount)
                h.parameter_facts = CSOsdiPrecisionSection(offset: UInt64(factsOffset), count: header.parameters.count, stride: UInt32(MemoryLayout<CSOsdiPrecisionParameterFact>.stride))
                h.simulation_facts = CSOsdiPrecisionSection(offset: UInt64(simulationOffset), count: UInt32(preparation.simulationParameters.count), stride: UInt32(MemoryLayout<CSOsdiPrecisionSimulationFact>.stride))
                h.raw_to_active = CSOsdiPrecisionSection(offset: UInt64(mapOffset), count: UInt32(topology.nodes.count), stride: 4)
                h.strings = CSOsdiPrecisionSection(offset: UInt64(stringsOffset), count: UInt32(poolCount), stride: 1)
                h.setup_bytes = UInt64(setupCount); store(h, at: 0)
            }
            let programDigest = try digest(program, control: control)
            let setupDigest = try digest(setup, control: control)
            try control.checkpoint(workUnits: 0)
            return OSDIPrecisionProgram(modelReference: modelReference, descriptorIndex: descriptorIndex, topology: topology,
                program: program, setup: setup, programDigest: programDigest, setupDigest: setupDigest,
                semanticProfile: semanticProfile, reservation: resultReservation)
        }
    }
    private static func digest(_ bytes: OSDIPrecisionSnapshotBuffer,
        control: any OSDIPrecisionSnapshotControlling) throws -> ContentDigest {
        // CF's pinned public digester requires Array. Admit this exact initialized
        // copy and fixed ASCII digest conversion payload before calling that API.
        let payload = try add(bytes.count, MemoryLayout<[UInt8]>.stride + 16 + 64 + 64 + 64)
        let scratch = try control.reserveStorage(byteCount: UInt64(payload))
        return try withExtendedLifetime(scratch) {
            try control.checkpoint(workUnits: UInt64(try add(try multiply(bytes.count, 2), 16 + 64 + 64 + 64)))
            return try bytes.withBytes { try OSDIPrecisionProgramBuilder.digest(Array($0)) }
        }
    }
}
