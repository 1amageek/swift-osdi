import Foundation
import SignoffToolSupport

public enum FixtureBuildError: Error {
    case invalidInput(String)
    case commandFailed(TimedProcessResult)
    case changedInput(String)
    case lockFailed(Int32)
    case lockTimeout
    case publicationFailed(Int32)
    case pendingCleanup(process: TimedProcessError, directory: URL)
    case batch([any Error])
    case cleanup(primary: any Error, secondary: any Error)
}
