import Foundation
import OSDIContracts
import OSDIRuntime
import Testing

@Suite(.serialized, .timeLimit(.minutes(1)))
struct OSDIReactiveMetadataTests {
    @Test
    func reactiveOffsetPresenceDoesNotFollowSampledChargeOrJacobian() async throws {
        let nature = try OSDINatureReference(kind: 0, index: 0)
        let manual = try OSDINodeInfo(index: 0, name: "p", units: "V", residualUnits: "A",
            isFlow: false, unknownNature: nature, residualNature: nature, activeUnknownIndex: 0)
        #expect(manual.hasReactiveResidual)
        let absent = try OSDINodeInfo(index: 0, name: "p", units: "V", residualUnits: "A",
            isFlow: false, unknownNature: nature, residualNature: nature, activeUnknownIndex: 0,
            hasReactiveResidual: false)
        #expect(!absent.hasReactiveResidual)

        let fixtures = try await ProtocolFixtureCache.shared.fixtures()
        let limits = try EvaluationFixtureRequest.limits()
        for (name, present) in [("time_state_charge", true), ("no_charge", false)] {
            guard let fixture = fixtures.protocolLibraries[name] else { throw EvaluationFixtureError.invalidReference(name) }
            let library = try await EvaluationArtifact(fixture.path).load()
            var model = try OSDINativeModelPreparer().prepare(library: library, descriptorIndex: 0,
                options: OSDIPreparationOptions(parameters: [], simulationParameters: [], temperatureKelvin: 300.15,
                    connectedTerminalCount: 1, limits: limits))
            #expect(model.topology.nodes.count == 1)
            #expect(model.topology.nodes[0].hasReactiveResidual == present)
            let x = [3.0]
            for (time, state) in [(0.0, 0.0), (0.5, 2.0), (0.5, 7.0), (0.75, 2.0)] {
                let previous = [state, 7.0]
                let context = try OSDIEvaluationContext(mode: .transient, timeSeconds: time)
                let result = try OSDINativeModelEvaluator().evaluate(model: &model, context: context,
                    unknowns: x.span, previousState: previous.span) { ($0.q[0], $0.c[0]) }
                #expect(result.0 == (present ? time + state : 0))
                #expect(result.1 == 0)
                #expect(model.topology.nodes[0].hasReactiveResidual == present)
            }
            model.close()
            #expect(model.topology.nodes[0].hasReactiveResidual == present)
            try library.close()
        }
    }
}
