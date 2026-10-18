import Foundation
import OSDIRuntime

/// Owned values exist only at the external JSON evidence boundary.
struct ConsumerSnapshot: Codable, Equatable {
    let f: [Double]
    let q: [Double]
    let g: [Double]
    let c: [Double]
    let nextState: [Double]

    static func capture(_ view: borrowing OSDIEvaluationView) -> Self {
        Self(f: copy(view.f), q: copy(view.q), g: copy(view.g), c: copy(view.c),
             nextState: copy(view.nextState))
    }

    private static func copy(_ values: borrowing Span<Double>) -> [Double] {
        var result: [Double] = []
        result.reserveCapacity(values.count)
        for index in 0..<values.count { result.append(values[index]) }
        return result
    }
}
