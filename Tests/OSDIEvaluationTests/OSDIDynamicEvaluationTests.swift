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

    @Test
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
    func contextValidationAndNegativeSpectraNeverPublishViews() async throws {
        let invalid: [(OSDIEvaluationMode, Double, Double?)] = [(.ac, 1, nil), (.transient, -.infinity, nil),
            (.transientInitialization, 1, nil), (.noise, 0, 0), (.dc, 0, 1)]
        for (mode, time, frequency) in invalid {
            #expect(throws: OSDIContractError.invalidEvaluationContext) {
                try OSDIEvaluationContext(mode: mode, timeSeconds: time, noiseFrequencyHz: frequency)
            }
        }
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let options = try fixtures.point("rc-linear").input.options()
        let context = try OSDIEvaluationContext(mode: .noise, timeSeconds: 0, noiseFrequencyHz: 10)
        for name in ["negative_noise", "nonfinite_noise"] {
            guard let fixture = fixtures.protocolLibraries[name] else { throw EvaluationFixtureError.invalidReference(name) }
            let library = try await EvaluationArtifact(fixture.path).load()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
                options: OSDIPreparationOptions(parameters: [], simulationParameters: [],
                    temperatureKelvin: 300.15, connectedTerminalCount: 1, limits: options.limits))
            let x = [1.0], previous = [2.0, 7.0]
            var called = false
            do {
                try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                    unknowns: x.span, previousState: previous.span) { _ in called = true }
                Issue.record("Invalid spectrum was admitted")
            } catch { #expect(!called) }
            model.close(); try library.close()
        }
    }

    @Test
    func dynamicTrialsPreserveExplicitAcceptedNativeState() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        guard let fixture = fixtures.protocolLibraries["state"] else { throw EvaluationFixtureError.invalidReference("state") }
        let library = try await EvaluationArtifact(fixture.path).load()
        let limits = try fixtures.point("rc-linear").input.options().limits
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
    @Test
    func fourPinnedFamiliesMatchIndependentDynamicPointAndNoiseABI() async throws {
        guard let path = ProcessInfo.processInfo.environment["OSDI_S4_FAMILIES_MANIFEST"] else {
            throw EvaluationFixtureError.requiredEnvironment("OSDI_S4_FAMILIES_MANIFEST")
        }
        let manifest = try JSONDecoder().decode(DynamicFamilyManifest.self, from: Data(contentsOf: URL(fileURLWithPath: path)))
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let oracle = URL(fileURLWithPath: try fixtures.point("rc-linear").library)
            .deletingLastPathComponent().appendingPathComponent("oracle")
        struct Raw: Decodable { let f: [Double]; let q: [Double]; let g: [Double]; let c: [Double]; let noiseDensities: [Double] }
        for id in ["bsim-p-selfheat-loaded", "cmg-n-selfheat-series-loaded", "img-n-thermal", "soi-n-series-thermal"] {
            guard let point = manifest.cases.first(where: { $0.id == id }), let input = point.input.devices.first,
                  let fact = manifest.libraries.first(where: { $0.id == input.library }),
                  let unknowns = point.reference.unknowns, let rawNodes = point.reference.devices?.first?.rawNodes else {
                throw EvaluationFixtureError.invalidReference(id)
            }
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
            for node in rawNodes {
                let value: Double
                if let key = node.unknownID {
                    guard let supplied = values[key] else { throw EvaluationFixtureError.invalidReference(key) }
                    value = supplied
                } else { value = 0 }
                base.append("v:\(node.name)=\(value)")
                if let index = model.topology.nodes[node.index].activeUnknownIndex { x[index] = value }
            }
            #expect(!model.noiseSources.isEmpty)
            let contexts: [(OSDIEvaluationMode, Double, Double?)] = [(.ac, 0, nil), (.transientInitialization, 0, nil),
                (.transient, 1e-8, nil), (.noise, 0, 100), (.noise, 0, 1e6)]
            for (mode, time, frequency) in contexts {
                let context = try OSDIEvaluationContext(mode: mode, timeSeconds: time, noiseFrequencyHz: frequency)
                let bytes = try await runOracle(oracle, base + ["e:mode=\(mode.rawValue)", "e:time=\(time)", "e:frequency=\(frequency ?? 0)"])
                let reference = try JSONDecoder().decode(Raw.self, from: bytes)
                let result = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                    unknowns: x.span, previousState: input.previousState.span) { view in
                    (copyValues(view.f), copyValues(view.q), copyValues(view.g), copyValues(view.c), copyValues(view.noiseDensities))
                }
                for (actual, expected) in [(result.0, reference.f), (result.1, reference.q), (result.2, reference.g),
                                           (result.3, reference.c), (result.4, reference.noiseDensities)] {
                    #expect(actual.count == expected.count)
                    for index in expected.indices {
                        #expect(actual[index].isFinite && abs(actual[index] - expected[index]) <= 1e-14 + abs(expected[index]) * 1e-11)
                    }
                }
                if mode == .noise { #expect(result.4.contains { $0 > 0 }) }
            }
            model.close(); try library.close()
        }
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
        let deadline = ContinuousClock.now.advanced(by: .seconds(20))
        while process.isRunning {
            guard ContinuousClock.now < deadline else { process.terminate(); throw EvaluationFixtureError.timeout }
            try await Task.sleep(for: .milliseconds(10))
        }
        let data = try Data(contentsOf: destination)
        guard process.terminationStatus == 0 else {
            throw EvaluationFixtureError.invalidReference(String(decoding: data, as: UTF8.self))
        }
        return data
    }

}
