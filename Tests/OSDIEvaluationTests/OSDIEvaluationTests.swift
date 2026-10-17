import Foundation
import OSDIContracts
import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIEvaluationTests {
    @Test
    func independentFiniteDifferenceQualificationRequiresEveryCoordinate() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        #expect(fixtures.finiteDifferenceChecks == 1204)
        #expect(fixtures.finiteDifferenceStatus == "passed", "Every active coordinate must satisfy the full-residual derivative contract")
    }

    @Test
    func allNineteenActualModelPointsMatchIndependentRawABI() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        #expect(fixtures.cases.count == 19)
        #expect(fixtures.compilerProfile == "full-residuals")
        #expect(fixtures.bsimSHA256 == ProcessInfo.processInfo.environment["OSDI_BSIM_SHA256"])
        #expect(fixtures.defaultProfileDiscriminator.reproduced)
        #expect(fixtures.defaultProfileDiscriminator.defaultProfile == "default")
        #expect(fixtures.defaultProfileDiscriminator.qualifiedProfile == "full-residuals")
        for point in fixtures.cases {
            let library = try await EvaluationArtifact(point.library).load()
            let options = try point.input.options()
            var model = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: point.input.descriptorIndex, options: options)
            let reference = point.reference
            #expect(model.topology.nodes.map(\.name) == reference.nodes, "\(point.id)")
            #expect(model.topology.nodes.map(\.activeUnknownIndex) == reference.collapseMap, "\(point.id)")
            #expect(model.topology.activeUnknownCount == reference.localUnknowns.count)
            #expect(model.topology.stateCount == reference.stateCount)
            for index in reference.nodes.indices {
                #expect(model.topology.nodes[index].units == reference.nodeMetadata[index].units)
                #expect(model.topology.nodes[index].residualUnits == reference.nodeMetadata[index].residualUnits)
                #expect(model.topology.nodes[index].isFlow == reference.nodeMetadata[index].isFlow)
            }
            try compareParameters(model, reference)
            let previous = Array(repeating: 0.0, count: reference.stateCount)
            let result = try OSDINativeModelEvaluator().evaluate(model: &model,
                unknowns: reference.localUnknowns.span, previousState: previous.span) { Snapshot.capture($0) }
            let actual = try result.get()
            compare(actual.f, reference.f, "\(point.id).f")
            compare(actual.q, reference.q, "\(point.id).q")
            compare(actual.g, reference.g, "\(point.id).g")
            compare(actual.c, reference.c, "\(point.id).c")
            compare(actual.nextState, reference.nextState, "\(point.id).nextState")
            #expect(actual.flags == reference.modelFlags)
            model.close()
            try library.close()
        }
    }

    @Test
    func analyticRCAndFlowUnitsRemainDiscriminating() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        for (name, conductance, capacitance) in [("rc-linear", 0.001, 2e-12), ("rc-first", 0.001, 2e-12), ("rc-second", 0.003, 4e-12)] {
            let reference = try fixtures.point(name).reference
            compare(reference.f, [conductance, -conductance], "\(name).analyticF")
            compare(reference.q, [capacitance, -capacitance], "\(name).analyticQ")
            compare(reference.g, [conductance, -conductance, -conductance, conductance], "\(name).analyticG")
            compare(reference.c, [capacitance, -capacitance, -capacitance, capacitance], "\(name).analyticC")
        }
        let flow = try fixtures.point("flow-unit").reference
        #expect(flow.nodeMetadata.contains { $0.isFlow && $0.units == "A" && $0.residualUnits == "V" })
        let collapsed = try fixtures.point("collapse-on").reference
        let separate = try fixtures.point("collapse-off").reference
        #expect(collapsed.localUnknowns.count == 2)
        #expect(separate.localUnknowns.count == 3)
        #expect(collapsed.collapseMap == [0, 1, 1])
        #expect(separate.collapseMap == [0, 1, 2])
    }

    @Test
    func invalidTypedAssignmentsAndNativeBoundsFailWithoutPoisoningLibrary() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let point = try fixtures.point("typed-default")
        let library = try await EvaluationArtifact(point.library).load()
        let cases: [([OSDIParameterAssignment], Int32)] = [
            ([try assignment("missing", .model, .real(1))], 13),
            ([try assignment("conductance", .model, .integer(1))], 14),
            ([try assignment("mode", .model, .real(1))], 14),
            ([try assignment("polarity", .instance, .integer(1))], 15),
            ([try assignment("gain", .model, .real(0.001)), try assignment("conductance", .model, .real(0.002))], 10),
            ([try assignment("scale_alias", .instance, .real(2)), try assignment("scale", .instance, .real(3))], 10),
            ([try assignment("polarity", .model, .integer(0))], 16),
            ([try assignment("scale", .instance, .real(0))], 16)
        ]
        for (parameters, expectedCode) in cases {
            let options = try point.input.options(parameters: parameters)
            do {
                var model = try OSDINativeModelPreparer().prepare(library: library,
                    descriptorIndex: 0, options: options)
                model.close()
                Issue.record("Rejected assignment unexpectedly prepared")
            } catch {
                guard case .native(let issue) = error else { throw error }
                #expect(issue.code == expectedCode)
                if expectedCode == 10 { #expect(issue.field == "parameter.duplicate") }
                if expectedCode == 16 {
                    #expect(issue.setupErrorCount > 0 || issue.modelFlags != 0)
                    #expect(issue.setupErrorCode == 1 || issue.modelFlags != 0)
                }
            }
        }
        let options = try point.input.options()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        model.close()
        try library.close()
    }

    @Test
    func invalidContractsAndNativeResourceBudgetsRemainFailures() async throws {
        #expect(throws: OSDIContractError.invalidParameterValue) {
            try OSDIParameterAssignment(name: "scale", scope: .instance, value: .real(.nan))
        }
        #expect(throws: OSDIContractError.invalidParameterValue) {
            try OSDIParameterAssignment(name: "mode", scope: .model, value: .string("bad\0value"))
        }
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let point = try fixtures.point("typed-default")
        let library = try await EvaluationArtifact(point.library).load()
        let tiny = try OSDIEvaluationLimits(maximumWorkspaceBytes: 1,
            maximumDiagnosticCount: 1, maximumDiagnosticBytes: 1, maximumStringBytes: 1)
        let bridgeOptions = try point.input.options(limits: tiny)
        do {
            var model = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: 0, options: bridgeOptions)
            model.close()
            Issue.record("Insufficient workspace admitted lexical bridge tables")
        } catch { guard case .resourceLimit = error else { throw error } }
        // Empty lexical tables reach the native workspace gate without a bridge allocation.
        let options = try OSDIPreparationOptions(parameters: [], simulationParameters: [],
            temperatureKelvin: 300.15, connectedTerminalCount: 2, limits: tiny)
        do {
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
            model.close()
            Issue.record("Insufficient workspace prepared a model")
        } catch {
            guard case .native(let issue) = error else { throw error }
            #expect(issue.code == 7)
        }
        #expect(throws: OSDIContractError.invalidPreparation) {
            try OSDIPreparationOptions(parameters: [], simulationParameters: [],
                temperatureKelvin: 0, connectedTerminalCount: 2, limits: EvaluationFixtureRequest.limits())
        }
        try library.close()
    }

    @Test
    func wrongDimensionsNonfiniteUnknownsClosedModelAndExclusivePreparationFail() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let point = try fixtures.point("rc-linear")
        let library = try await EvaluationArtifact(point.library).load()
        let options = try point.input.options()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        do {
            var second = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
            second.close()
            Issue.record("A second mutable native model prepared on the same image")
        } catch { guard case .libraryInUse = error else { throw error } }
        do {
            try library.close()
            Issue.record("A prepared native model did not retain the image")
        } catch { guard case .libraryInUse = error else { throw error } }
        let empty: [Double] = []
        let extraState = [0.0]
        let short = [0.8]
        var called = false
        do {
            try OSDINativeModelEvaluator().evaluate(model: &model, unknowns: short.span,
                previousState: empty.span) { _ in called = true }
            Issue.record("Wrong active vector size was admitted")
        } catch { guard case .invalidRange = error else { throw error } }
        do {
            try OSDINativeModelEvaluator().evaluate(model: &model, unknowns: point.reference.localUnknowns.span,
                previousState: extraState.span) { _ in called = true }
            Issue.record("Wrong previous-state size was admitted")
        } catch { guard case .invalidRange = error else { throw error } }
        let nonfinite = [Double.nan, 0]
        do {
            try OSDINativeModelEvaluator().evaluate(model: &model, unknowns: nonfinite.span,
                previousState: empty.span) { _ in called = true }
            Issue.record("Nonfinite active input was admitted")
        } catch {
            guard case .native(let issue) = error else { throw error }
            #expect(issue.code == 18)
        }
        #expect(!called)
        model.close()
        model.close()
        do {
            try OSDINativeModelEvaluator().evaluate(model: &model, unknowns: point.reference.localUnknowns.span,
                previousState: empty.span) { _ in called = true }
            Issue.record("A closed prepared model evaluated")
        } catch { guard case .closed = error else { throw error } }
        #expect(!called)
        try library.close()
    }

    @Test
    func concurrentMetadataAndReentrantCallbackRespectPreparedOwner() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let point = try fixtures.point("rc-linear")
        let library = try await EvaluationArtifact(point.library).load()
        let options = try point.input.options()
        var model = try OSDINativeModelPreparer().prepare(library: library,
            descriptorIndex: 0, options: options)
        // The parent task owns the model throughout every child admission attempt.
        let failures = await withTaskGroup(of: String?.self, returning: [String].self) { group in
            for _ in 0..<8 {
                group.addTask { Self.refusalWhilePrepared(library, options) }
            }
            var failures: [String] = []
            for await failure in group { if let failure { failures.append(failure) } }
            return failures
        }
        #expect(failures.isEmpty, "\(failures)")
        let previous: [Double] = []
        let callbackFailure = try OSDINativeModelEvaluator().evaluate(model: &model,
            unknowns: point.reference.localUnknowns.span, previousState: previous.span) { view in
                #expect(view.f.count == 2)
                return Self.refusalWhilePrepared(library, options)
            }
        #expect(callbackFailure == nil, "\(callbackFailure ?? "")")
        model.close()
        var second = try OSDINativeModelPreparer().prepare(library: library,
            descriptorIndex: 0, options: options)
        second.close()
        try library.close()
    }

    @Test
    func explicitPreviousStateProducesCandidateWithoutImplicitAcceptance() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let path = try protocolPath("state", fixtures)
        let library = try await EvaluationArtifact(path).load()
        let options = try protocolOptions()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        #expect(model.topology.stateCount == 2)
        let inputs = [3.0], previous = [2.0, 7.0]
        let first = try evaluate(&model, inputs, previous)
        compare(first.f, [5], "state.f")
        compare(first.nextState, [5, 7], "state.candidate")
        let second = try evaluate(&model, inputs, previous)
        compare(second.nextState, [5, 7], "state.noImplicitAccept")
        let acceptedByCaller = try evaluate(&model, inputs, first.nextState)
        compare(acceptedByCaller.nextState, [8, 7], "state.explicitAccept")
        model.close()
        try library.close()
    }

    @Test
    func flagsAndNonfiniteOutputNeverPublishAnEvaluationView() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        for (name, flags, code) in [("fatal", UInt32(2), Int32(17)), ("finish", 4, 17),
                                   ("stop", 8, 17), ("limiting", 1, 17),
                                   ("unknown_flags", 0x80000000, 17), ("nonfinite_output", 0, 19)] {
            let library = try await EvaluationArtifact(protocolPath(name, fixtures)).load()
            let options = try protocolOptions()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
            let values = [3.0], previous = [2.0, 7.0]
            var called = false
            do {
                try OSDINativeModelEvaluator().evaluate(model: &model, unknowns: values.span,
                    previousState: previous.span) { _ in called = true }
                Issue.record("Native failure \(name) published an evaluation")
            } catch {
                guard case .native(let issue) = error else { throw error }
                #expect(issue.code == code)
                #expect(issue.modelFlags == flags)
            }
            #expect(!called)
            model.close()
            try library.close()
        }
    }

    @Test
    func ownedAndLiteralDiagnosticsAreCopiedAndBudgetsAreEnforced() async throws {
        let fixtures = try await EvaluationFixtureCache.shared.fixtures()
        let library = try await EvaluationArtifact(protocolPath("diagnostics", fixtures)).load()
        let options = try protocolOptions()
        var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0, options: options)
        let first = try evaluate(&model, [3], [2, 7])
        #expect(first.diagnostics.map(\.message) == ["owned protocol warning", "literal protocol warning"])
        #expect(first.diagnostics.map(\.level) == [3, 19])
        _ = try evaluate(&model, [4], [2, 7])
        model.close()
        try library.close()
        #expect(first.diagnostics[0].message == "owned protocol warning")
        for (name, expectedCode) in [("diagnostic_overflow", Int32(20)), ("logged_error", 17)] {
            let image = try await EvaluationArtifact(protocolPath(name, fixtures)).load()
            let limits = try OSDIEvaluationLimits(maximumWorkspaceBytes: 1_048_576,
                maximumDiagnosticCount: 1, maximumDiagnosticBytes: 4096, maximumStringBytes: 4096)
            let configuration = try protocolOptions(limits: limits)
            var prepared = try OSDINativeModelPreparer().prepare(library: image, descriptorIndex: 0, options: configuration)
            do {
                _ = try evaluate(&prepared, [3], [2, 7])
                Issue.record("Diagnostic failure \(name) was admitted")
            } catch let error as OSDIError {
                guard case .native(let issue) = error else { throw error }
                #expect(issue.code == expectedCode)
            }
            prepared.close()
            try image.close()
        }
    }

    private static func refusalWhilePrepared(_ library: OSDILibrary,
                                             _ options: OSDIPreparationOptions) -> String? {
        guard library.descriptors.count == 1, library.descriptors[0].name == "linear_rc" else {
            return "Concurrent immutable descriptor read changed"
        }
        do {
            try library.close()
            return "Close admitted while the prepared owner was live"
        } catch { guard case .libraryInUse = error else { return "Unexpected close error: \(error)" } }
        do {
            var second = try OSDINativeModelPreparer().prepare(library: library,
                descriptorIndex: 0, options: options)
            second.close()
            return "Second preparation admitted while the prepared owner was live"
        } catch { guard case .libraryInUse = error else { return "Unexpected preparation error: \(error)" } }
        return nil
    }

    private func compareParameters(_ model: borrowing OSDIPreparedModel, _ reference: EvaluationReference) throws {
        for parameter in reference.parameters {
            let metadata = try #require(model.parameterMetadata.first { $0.index == parameter.index })
            #expect(metadata.names == parameter.names)
            #expect(metadata.kind.rawValue == parameter.kind)
            #expect(metadata.type.rawValue == parameter.type)
            let modelValue = try #require(model.resolvedParameters.first { $0.parameter.index == parameter.index && $0.scope == .model })
            #expect(modelValue.value == (try parameter.modelValue.value()))
            #expect(modelValue.isGiven == parameter.modelGiven)
            if let expected = parameter.instanceValue, let given = parameter.instanceGiven {
                let instance = try #require(model.resolvedParameters.first { $0.parameter.index == parameter.index && $0.scope == .instance })
                #expect(instance.value == (try expected.value()))
                #expect(instance.isGiven == given)
            }
        }
    }

    private func assignment(_ name: String, _ scope: OSDIParameterScope,
                            _ value: OSDIParameterValue) throws -> OSDIParameterAssignment {
        try OSDIParameterAssignment(name: name, scope: scope, value: value)
    }

    private func protocolPath(_ name: String, _ fixtures: EvaluationFixtureManifest) throws -> String {
        guard let fixture = fixtures.protocolLibraries[name] else {
            throw EvaluationFixtureError.invalidReference("missing protocol fixture \(name)")
        }
        return fixture.path
    }

    private func protocolOptions(limits: OSDIEvaluationLimits? = nil) throws -> OSDIPreparationOptions {
        try OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
            connectedTerminalCount: 1, limits: limits ?? EvaluationFixtureRequest.limits())
    }

    private func evaluate(_ model: inout OSDIPreparedModel, _ values: [Double],
                          _ previous: [Double]) throws -> Snapshot {
        let result = try OSDINativeModelEvaluator().evaluate(model: &model,
            unknowns: values.span, previousState: previous.span) { Snapshot.capture($0) }
        return try result.get()
    }

    private func compare(_ actual: [Double], _ expected: [Double], _ field: String) {
        #expect(actual.count == expected.count, "\(field)")
        guard actual.count == expected.count else { return }
        for index in actual.indices {
            #expect(actual[index].bitPattern == expected[index].bitPattern, "\(field)[\(index)]")
        }
    }

    private struct Snapshot {
        let f: [Double]
        let q: [Double]
        let g: [Double]
        let c: [Double]
        let nextState: [Double]
        let flags: UInt32
        let diagnostics: [OSDIDiagnostic]

        static func capture(_ view: borrowing OSDIEvaluationView) -> Result<Self, OSDIError> {
            var f: [Double] = [], q: [Double] = [], g: [Double] = [], c: [Double] = [], next: [Double] = []
            for index in 0..<view.f.count { f.append(view.f[index]); q.append(view.q[index]) }
            for index in 0..<view.g.count { g.append(view.g[index]); c.append(view.c[index]) }
            for index in 0..<view.nextState.count { next.append(view.nextState[index]) }
            var diagnostics: [OSDIDiagnostic] = []
            do {
                for index in 0..<view.diagnosticCount { diagnostics.append(try view.diagnostic(at: index)) }
            } catch { return .failure(error) }
            return .success(Self(f: f, q: q, g: g, c: c, nextState: next,
                                 flags: view.modelFlags, diagnostics: diagnostics))
        }
    }
}
