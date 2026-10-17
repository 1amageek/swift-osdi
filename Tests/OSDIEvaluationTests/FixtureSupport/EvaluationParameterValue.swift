import OSDIContracts

struct EvaluationParameterValue: Decodable, Sendable {
    let kind: String
    let real: Double?
    let integer: Int32?
    let string: String?

    func value() throws -> OSDIParameterValue {
        switch kind {
        case "real":
            guard let real else { throw EvaluationFixtureError.invalidReference("real value") }
            return .real(real)
        case "integer":
            guard let integer else { throw EvaluationFixtureError.invalidReference("integer value") }
            return .integer(integer)
        case "string":
            guard let string else { throw EvaluationFixtureError.invalidReference("string value") }
            return .string(string)
        default: throw EvaluationFixtureError.invalidReference("parameter kind \(kind)")
        }
    }
}
