import Darwin
import Foundation
import OSDIContracts
import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIDynamicEvaluationTests {
    private func copyValues(_ span: borrowing Span<Double>) -> [Double] {
        var values: [Double] = []; values.reserveCapacity(span.count)
        for index in 0..<span.count { values.append(span[index]) }
        return values
    }

    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires explicit OSDI_MODEL_QUALIFICATION=1 and exact external compiler/model inputs"))
    func explicitModesTimeNoiseAndFreshBoundsUseActualGeneratedCallbacks() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let point = try fixtures.point("rc-linear")
        let path = URL(fileURLWithPath: point.library).deletingLastPathComponent()
            .appendingPathComponent("dynamic_profile.osdi").path
        let library = try await EvaluationArtifact(path).load()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
            options: point.input.options())
        let inputs = [2.0, 0.0], previous: [Double] = []
        #expect(model.noiseSources.count == 2)
        for (mode, time, phase) in [(OSDIEvaluationMode.dc, 0.0, 33.0), (.ac, 0, 2),
                                    (.transient, 1.5, 4), (.transientInitialization, 0, 44), (.noise, 0, 18)] {
            let context = try OSDIEvaluationContext(mode: mode, timeSeconds: time,
                noiseFrequencyHz: mode == .noise ? 100 : nil)
            let result = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                unknowns: inputs.span, previousState: previous.span) { view in
                (view.f[0], view.q[0], view.g[0], view.c[0], view.noiseDensities.count,
                 view.maximumTimestep, copyValues(view.noiseDensities))
            }
            #expect(abs(result.0 - (phase + time + 0.002)) < 1e-12)
            #expect(abs(result.1 - 4e-12) < 1e-25)
            #expect(abs(result.2 - 0.001) < 1e-15)
            #expect(abs(result.3 - 2e-12) < 1e-25)
            #expect(result.4 == (mode == .noise ? 2 : 0))
            #expect(result.5 == 0.25 + time)
            if mode == .noise {
                for source in model.noiseSources {
                    #expect(source.positiveNodeIndex < model.topology.nodes.count)
                    let expected = source.kind == .white ? 16e-18 : 9e-20
                    #expect(abs(result.6[source.index] - expected) <= expected * 1e-12)
                }
            }
        }
        let zero = [0.0, 0.0]
        let noBound = try OSDINativeModelEvaluator().evaluate(model: &model,
            unknowns: zero.span, previousState: previous.span) { $0.maximumTimestep }
        #expect(noBound == nil)
        let negative = [-1.0, 0.0]
        var called = false
        do {
            try OSDINativeModelEvaluator().evaluate(model: &model,
                unknowns: negative.span, previousState: previous.span) { _ in called = true }
            Issue.record("Invalid bound was admitted")
        } catch { #expect(!called) }
        model.close(); try library.close()
    }

    @Test
    func contextValidationAndNonfiniteSpectraNeverPublishViews() async throws {
        let invalid: [(OSDIEvaluationMode, Double, Double?)] = [(.ac, 1, nil), (.transient, -.infinity, nil),
            (.transientInitialization, 1, nil), (.noise, 0, 0), (.dc, 0, 1)]
        for (mode, time, frequency) in invalid {
            #expect(throws: OSDIContractError.invalidEvaluationContext) {
                try OSDIEvaluationContext(mode: mode, timeSeconds: time, noiseFrequencyHz: frequency)
            }
        }
        let fixtures = try await ProtocolFixtureCache.shared.fixtures()
        let limits = try EvaluationFixtureRequest.limits()
        let context = try OSDIEvaluationContext(mode: .noise, timeSeconds: 0, noiseFrequencyHz: 10)
        for name in ["nonfinite_noise"] {
            guard let fixture = fixtures.protocolLibraries[name] else { throw EvaluationFixtureError.invalidReference(name) }
            let library = try await EvaluationArtifact(fixture.path).load()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
                options: OSDIPreparationOptions(parameters: [], simulationParameters: [],
                    temperatureKelvin: 300.15, connectedTerminalCount: 1, limits: limits))
            let x = [1.0], previous = [2.0, 7.0]
            var called = false
            do {
                try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                    unknowns: x.span, previousState: previous.span) { _ in called = true }
                Issue.record("Nonfinite spectrum was admitted")
            } catch {
                guard case .native(let issue) = error else { throw error }
                #expect(issue.code == 19 && issue.field == "noise_density")
                #expect(!called)
            }
            model.close(); try library.close()
        }
    }

    @Test
    func signedWhiteNoiseUsesFixedBiasMagnitudeAndCanonicalZero() async throws {
        let fixtures = try await ProtocolFixtureCache.shared.fixtures()
        #expect(fixtures.protocolLibraries.count == 15)
        let limits = try EvaluationFixtureRequest.limits()
        let context = try OSDIEvaluationContext(mode: .noise, timeSeconds: 0, noiseFrequencyHz: 10)
        for (name, expected) in [("negative_noise", 1.0), ("negative_zero_noise", 0.0)] {
            let fixture = try #require(fixtures.protocolLibraries[name])
            let library = try await EvaluationArtifact(fixture.path).load()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
                options: OSDIPreparationOptions(parameters: [], simulationParameters: [],
                    temperatureKelvin: 300.15, connectedTerminalCount: 1, limits: limits))
            let x = [1.0], previous = [2.0, 7.0]
            let result = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                unknowns: x.span, previousState: previous.span) { copyValues($0.noiseDensities) }
            #expect(model.noiseSources.count == 1 && model.noiseSources[0].kind == .white)
            compareExact(result, [expected], name + ".fixedBiasPSD")
            model.close(); try library.close()
        }
    }

    @Test
    func dynamicTrialsPreserveExplicitAcceptedNativeState() async throws {
        let fixtures = try await ProtocolFixtureCache.shared.fixtures()
        guard let fixture = fixtures.protocolLibraries["state"] else { throw EvaluationFixtureError.invalidReference("state") }
        let library = try await EvaluationArtifact(fixture.path).load()
        let limits = try EvaluationFixtureRequest.limits()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
            options: OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
                connectedTerminalCount: 1, limits: limits))
        let x = [3.0], accepted = [2.0, 7.0]
        let context = try OSDIEvaluationContext(mode: .transient, timeSeconds: 0.5)
        let candidate = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
            unknowns: x.span, previousState: accepted.span) { copyValues($0.nextState) }
        let other = [9.0]
        _ = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
            unknowns: other.span, previousState: accepted.span) { copyValues($0.nextState) }
        let retry = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
            unknowns: x.span, previousState: accepted.span) { copyValues($0.nextState) }
        #expect(candidate == [5, 7]); #expect(retry == candidate)
        model.close(); try library.close()
    }
    @Test(.enabled(if: ProcessInfo.processInfo.environment["OSDI_MODEL_QUALIFICATION"] == "1", "Requires explicit OSDI_MODEL_QUALIFICATION=1 and exact external compiler/model inputs"))
    func allAvailableFamilyPointsMatchABIWithExplicitNoiseAdmission() async throws {
        guard let path = ProcessInfo.processInfo.environment["OSDI_S4_FAMILIES_MANIFEST"] else {
            throw EvaluationFixtureError.requiredEnvironment("OSDI_S4_FAMILIES_MANIFEST")
        }
        let manifest = try JSONDecoder().decode(DynamicFamilyManifest.self, from: Data(contentsOf: URL(fileURLWithPath: path)))
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let oracle = URL(fileURLWithPath: try fixtures.point("rc-linear").library)
            .deletingLastPathComponent().appendingPathComponent("oracle")
        let families: Set<String> = ["bsim", "cmg", "img", "soi", "soi101"]
        let requiredFamilies = families.intersection(manifest.libraries.map(\.id))
        try #require(Set(["bsim", "cmg", "img"]).isSubset(of: requiredFamilies))
        try #require(requiredFamilies.contains("soi101") || requiredFamilies.contains("soi"))
        let points = manifest.cases.filter { $0.input.devices.contains { families.contains($0.library) } }
        #expect(Set(points.flatMap { $0.input.devices.map(\.library) }) == requiredFamilies)
        // These are exact-artifact regressions of the supplied manifest, including its SOI baseline.
        var derivativePoints: Set<String> = ["bsim-n-loaded", "bsim-p-loaded", "bsim-p-selfheat-loaded",
            "cmg-n-loaded", "cmg-p-loaded", "cmg-n-selfheat-series-loaded", "img-n-forward", "img-p-forward",
            "img-n-thermal"]
        if requiredFamilies.contains("soi") {
            derivativePoints.formUnion(["soi-n-forward", "soi-p-forward", "soi-n-series-thermal"])
        }
        if requiredFamilies.contains("soi101") {
            for polarity in ["n", "p"] {
                for configuration in ["pd-forward", "dd-forward", "dd-private-thermal-internal"] {
                    derivativePoints.insert("soi101-\(polarity)-\(configuration)")
                }
            }
        }
        #expect(derivativePoints.isSubset(of: Set(points.map(\.id))))
        for point in points {
            let input = try #require(point.input.devices.first)
            try #require(point.input.devices.count == 1, "\(point.id): one-device point input required")
            let fact = try #require(manifest.libraries.first { $0.id == input.library })
            let unknowns = try #require(point.reference.unknowns, "\(point.id): converged manifest seed required")
            let rawNodes = try #require(point.reference.devices?.first?.rawNodes)
            let artifact = try EvaluationArtifact(fact.path)
            #expect(artifact.intent.expectedReference.digest.hexadecimalValue == fact.sha256)
            let library = try await artifact.load()
            let options = try OSDIPreparationOptions(parameters: input.parameters.map { try $0.assignment() },
                simulationParameters: [OSDISimulationParameter(name: "gmin", value: .real(0))],
                temperatureKelvin: input.temperatureKelvin, connectedTerminalCount: input.connectedTerminalCount,
                limits: EvaluationFixtureRequest.limits())
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: input.descriptorIndex, options: options)
            let values = Dictionary(uniqueKeysWithValues: unknowns.map { ($0.id, $0.value) })
            var x = Array(repeating: 0.0, count: model.topology.activeUnknownCount)
            var base = [fact.path, String(input.descriptorIndex), String(input.temperatureKelvin), String(input.connectedTerminalCount)]
            for parameter in input.parameters {
                let scope = parameter.scope == "model" ? "m" : "i"
                switch try parameter.value.value() {
                case .real(let value): base.append("p:\(scope):r:\(parameter.name)=\(value)")
                case .integer(let value): base.append("p:\(scope):i:\(parameter.name)=\(value)")
                case .string(let value): base.append("p:\(scope):s:\(parameter.name)=\(value)")
                }
            }
            try #require(rawNodes.count == model.topology.nodes.count, "\(point.id): raw topology")
            for node in rawNodes {
                let value: Double
                if let key = node.unknownID {
                    value = try #require(values[key], "\(point.id): missing \(key)")
                } else { value = 0 }
                base.append("v:\(node.name)=\(value)")
                if let index = model.topology.nodes[node.index].activeUnknownIndex { x[index] = value }
            }
            // The independent oracle currently accepts an explicit zero accepted-state vector.
            try #require(input.previousState.allSatisfy { $0 == 0 })
            #expect(!model.noiseSources.isEmpty)
            let contexts: [(OSDIEvaluationMode, Double, Double?)] = [(.dc, 0, nil), (.ac, 0, nil),
                (.transientInitialization, 0, nil), (.transient, 1e-8, nil), (.noise, 0, 100), (.noise, 0, 1e6)]
            for (mode, time, frequency) in contexts {
                let context = try OSDIEvaluationContext(mode: mode, timeSeconds: time, noiseFrequencyHz: frequency)
                let bytes = try await runOracle(oracle, base + ["e:mode=\(mode.rawValue)", "e:time=\(time)", "e:frequency=\(frequency ?? 0)"])
                let decoder = JSONDecoder()
                decoder.nonConformingFloatDecodingStrategy = .convertFromString(
                    positiveInfinity: "Infinity", negativeInfinity: "-Infinity", nan: "NaN")
                let reference = try decoder.decode(DynamicFamilyManifest.Observation.self, from: bytes)
                #expect(model.topology.nodes.map(\.name) == reference.nodes, "\(point.id): descriptor order")
                #expect(model.topology.nodes.map(\.activeUnknownIndex) == reference.collapseMap, "\(point.id): collapsed mapping")
                #expect(model.topology.stateCount == reference.stateCount)
                try #require(model.noiseSources.count == reference.noiseSources.count)
                for (actual, expected) in zip(model.noiseSources, reference.noiseSources) {
                    #expect(actual.name == expected.name && actual.kind.rawValue == expected.kind)
                    #expect(actual.positiveNodeIndex == expected.positiveNodeIndex &&
                        actual.negativeNodeIndex == expected.negativeNodeIndex)
                }
                compareExact(x, reference.localUnknowns, "\(point.id).unknowns")
                try compareParameters(model, reference.parameters, point.id)
                let label = "\(point.id).\(mode).\(frequency ?? 0)"
                if reference.noiseDensities.contains(where: { !$0.isFinite }) {
                    for (index, density) in reference.noiseDensities.enumerated() where !density.isFinite {
                        print("Nonfinite raw noise rejected: \(label), source[\(index)] \(reference.noiseSources[index].name), raw PSD \(density)")
                    }
                    var called = false
                    do {
                        try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                            unknowns: x.span, previousState: input.previousState.span) { _ in called = true }
                        Issue.record("\(label): nonfinite raw-noise rejection contract published an evaluation")
                    } catch {
                        guard case .native(let issue) = error else {
                            throw EvaluationFixtureError.invalidReference("\(label): \(error)")
                        }
                        #expect(issue.code == 19 && issue.field == "noise_density", "\(label): \(issue)")
                    }
                    #expect(!called, "\(label): nonfinite raw-noise rejection contract callback")
                    continue
                }
                let result: PointValues
                do {
                    result = try evaluate(&model, context: context, x: x, previous: input.previousState)
                } catch {
                    throw EvaluationFixtureError.invalidReference("\(label): \(error)")
                }
                compareExact(result.f, reference.f, label + ".F")
                compareExact(result.q, reference.q, label + ".Q")
                compareExact(result.g, reference.g, label + ".G")
                compareExact(result.c, reference.c, label + ".C")
                compareExact(result.noise, reference.noiseDensities.map { abs($0) }, label + ".fixedBiasPSD")
                compareExact(result.nextState, reference.nextState, label + ".nextState")
                #expect(result.flags == reference.modelFlags, "\(label): model flags")
                #expect(result.maximumTimestep == reference.maximumTimestep, "\(label): bound")
                if mode == .noise { #expect(result.noise.contains { $0 > 0 }, "\(label): physical spectra") }
                if mode == .dc && derivativePoints.contains(point.id) {
                    try compareAllActiveDerivatives(&model, center: result, x: x,
                        previous: input.previousState, context: context, label: point.id)
                }
            }
            model.close(); try library.close()
        }
    }

    private struct PointValues {
        let f: [Double]
        let q: [Double]
        let g: [Double]
        let c: [Double]
        let noise: [Double]
        let nextState: [Double]
        let flags: UInt32
        let maximumTimestep: Double?
    }

    private func evaluate(_ model: inout OSDIPreparedModel, context: OSDIEvaluationContext,
                          x: [Double], previous: [Double]) throws -> PointValues {
        try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
            unknowns: x.span, previousState: previous.span) { view in
            PointValues(f: copyValues(view.f), q: copyValues(view.q), g: copyValues(view.g),
                c: copyValues(view.c), noise: copyValues(view.noiseDensities), nextState: copyValues(view.nextState),
                flags: view.modelFlags, maximumTimestep: view.maximumTimestep)
        }
    }

    private func compareExact(_ actual: [Double], _ expected: [Double], _ label: String) {
        #expect(actual.count == expected.count, "\(label): count")
        guard actual.count == expected.count else { return }
        for index in actual.indices {
            #expect(actual[index].bitPattern == expected[index].bitPattern, "\(label)[\(index)]")
        }
    }

    private func compareParameters(_ model: borrowing OSDIPreparedModel,
                                   _ parameters: [EvaluationReference.Parameter], _ label: String) throws {
        for parameter in parameters {
            let metadata = try #require(model.parameterMetadata.first { $0.index == parameter.index })
            #expect(metadata.names == parameter.names, "\(label): parameter aliases")
            #expect(metadata.kind.rawValue == parameter.kind && metadata.type.rawValue == parameter.type)
            let resolved = try #require(model.resolvedParameters.first { $0.parameter.index == parameter.index && $0.scope == .model })
            #expect(resolved.value == (try parameter.modelValue.value()))
            #expect(resolved.isGiven == parameter.modelGiven)
            if let expected = parameter.instanceValue, let given = parameter.instanceGiven {
                let instance = try #require(model.resolvedParameters.first { $0.parameter.index == parameter.index && $0.scope == .instance })
                #expect(instance.value == (try expected.value()))
                #expect(instance.isGiven == given)
            }
        }
    }

    private func compareAllActiveDerivatives(_ model: inout OSDIPreparedModel, center: PointValues,
                                             x: [Double], previous: [Double],
                                             context: OSDIEvaluationContext, label: String) throws {
        let mapping = model.topology.nodes.map(\.activeUnknownIndex)
        let n = mapping.count
        let count = x.count
        func contract(_ values: [Double]) -> [Double] {
            var result = Array(repeating: 0.0, count: count)
            for row in 0..<n { if let destination = mapping[row] { result[destination] += values[row] } }
            return result
        }
        let units = model.topology.nodes.map(\.residualUnits)
        var observableCount = 0, resolutionLimitedCount = 0
        for column in x.indices {
            let flatbandDirection = model.topology.nodes.contains {
                ($0.name == "e" || $0.name == "b") && $0.activeUnknownIndex == column
            }
            let localDDFlatband = label.hasPrefix("soi101-") && label.hasSuffix("-dd-forward") && flatbandDirection
            // SOI 101's DD flatband branch has about 3.88e-7 V half-width for these
            // exact seeds. A 2^-22 V step keeps both sides in that local source branch.
            let step = max(1, abs(x[column])) / (localDDFlatband ? 4_194_304 : 65_536)
            var pairs: [(width: Double, low: PointValues, high: PointValues)] = []
            for divisor in [1.0, 2.0] {
                let h = step / divisor
                var low = x, high = x
                low[column] -= h; high[column] += h
                pairs.append((high[column] - low[column],
                    try evaluate(&model, context: context, x: low, previous: previous),
                    try evaluate(&model, context: context, x: high, previous: previous)))
            }
            for (field, matrix, name) in [(\PointValues.f, \PointValues.g, "G"), (\PointValues.q, \PointValues.c, "C")] {
                var expected = Array(repeating: 0.0, count: count)
                var conditioningByUnit: [String: Double] = [:]
                var unitByRow = Array(repeating: "", count: count)
                for row in 0..<n {
                    // Retain raw equation magnitudes before incidence cancellation, including
                    // the local linearization scale of an equation whose DC residual is zero.
                    var scale = abs(center[keyPath: field][row])
                    for pair in pairs {
                        scale = max(scale, max(abs(pair.low[keyPath: field][row]), abs(pair.high[keyPath: field][row])))
                    }
                    for rawColumn in 0..<n {
                        let inputScale = mapping[rawColumn].map { max(1, abs(x[$0])) } ?? 1
                        scale += abs(center[keyPath: matrix][row*n + rawColumn]) * inputScale
                    }
                    conditioningByUnit[units[row], default: 0] += scale
                    guard let destination = mapping[row] else { continue }
                    unitByRow[destination] = units[row]
                    for rawColumn in 0..<n where mapping[rawColumn] == column {
                        expected[destination] += center[keyPath: matrix][row*n + rawColumn]
                    }
                }
                let coarseLow = contract(pairs[0].low[keyPath: field]), coarseHigh = contract(pairs[0].high[keyPath: field])
                let fineLow = contract(pairs[1].low[keyPath: field]), fineHigh = contract(pairs[1].high[keyPath: field])
                let operations = Double(n + 2)
                let gamma = operations * Double.ulpOfOne / (1 - operations * Double.ulpOfOne)
                for row in 0..<count {
                    let coarse = (coarseHigh[row] - coarseLow[row]) / pairs[0].width
                    let fine = (fineHigh[row] - fineLow[row]) / pairs[1].width
                    // This numerical observability floor is not an enclosure of model-internal
                    // rounding. Channels beneath it retain exact ABI coverage only.
                    let resolution = gamma * conditioningByUnit[unitByRow[row], default: 0] / pairs[1].width
                    let bound = resolution + 4*abs(fine - coarse)/3 + 64*expected[row].ulp
                    #expect(fine.isFinite && abs(fine - expected[row]) <= bound,
                        "\(label).\(name)[\(row),\(column)]: derivative \(fine), callback \(expected[row]), bound \(bound), resolution \(resolution)")
                    if max(abs(expected[row]), abs(fine)) <= resolution {
                        resolutionLimitedCount += 1
                    } else {
                        observableCount += 1
                    }
                }
            }
        }
        print("\(label): FD observable channels \(observableCount), resolution-limited channels \(resolutionLimitedCount); exact ABI comparison covers every channel")
    }

    private func runOracle(_ executable: URL, _ arguments: [String]) async throws -> Data {
        let destination = FileManager.default.temporaryDirectory.appendingPathComponent("osdi-S4-oracle-\(UUID().uuidString).json")
        guard FileManager.default.createFile(atPath: destination.path, contents: nil) else {
            throw EvaluationFixtureError.invalidReference("oracle output")
        }
        let handle = try FileHandle(forWritingTo: destination)
        defer { handle.closeFile() }
        let process = Process(); process.executableURL = executable; process.arguments = arguments
        process.standardOutput = handle
        let diagnostics = destination.appendingPathExtension("stderr")
        guard FileManager.default.createFile(atPath: diagnostics.path, contents: nil) else {
            throw EvaluationFixtureError.invalidReference("oracle diagnostics")
        }
        let errorHandle = try FileHandle(forWritingTo: diagnostics)
        defer { errorHandle.closeFile() }
        process.standardError = errorHandle
        try process.run()
        defer {
            if process.isRunning { _ = kill(process.processIdentifier, SIGKILL) }
        }
        let deadline = ContinuousClock.now.advanced(by: .seconds(20))
        while process.isRunning {
            guard ContinuousClock.now < deadline else { throw EvaluationFixtureError.timeout }
            try await Task.sleep(for: .milliseconds(10))
        }
        let data = try Data(contentsOf: destination)
        guard process.terminationStatus == 0 else {
            throw EvaluationFixtureError.invalidReference(String(decoding: try Data(contentsOf: diagnostics), as: UTF8.self))
        }
        return data
    }

}
