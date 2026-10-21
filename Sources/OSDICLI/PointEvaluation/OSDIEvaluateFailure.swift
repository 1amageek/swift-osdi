enum OSDIEvaluateFailure: Error {
    case invalidRequest(String)
    case invalidParameter(String)
    case invalidTemperature
    case invalidUnknowns(String)
    case unsupportedCapability(String)
    case reportRoundTrip
}
