/// Conditional formula semantics; none establishes a nonempty guard set or a flow.
public enum OSDIPrecisionChartMode: UInt32, Sendable {
    case record = 1
    case exactGraph = 2
    case formulaClosure = 3
}
