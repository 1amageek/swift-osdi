enum OSDIFixtureError: Error {
    case missingOpenVAF(String)
    case cannotCreateLog(String)
    case missingFixture(String)
    case buildFailed(Int32, String)
    case timeout
}
