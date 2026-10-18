enum ConsumerFailure: Error {
    case arguments
    case invariant(String)
    case cleanup(primary: any Error, secondary: any Error)
}
