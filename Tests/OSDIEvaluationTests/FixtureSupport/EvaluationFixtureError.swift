enum EvaluationFixtureError: Error {
    case requiredEnvironment(String)
    case invalidReference(String)
    case buildFailed(Int32, String)
    case timeout
    case cleanup(any Error, any Error)
}
