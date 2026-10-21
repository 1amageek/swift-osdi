import Foundation

@main
enum OSDICLIEntry {
    private static let usage = "Usage: osdi <inspect|evaluate|precision> --request REQUEST.json --output REPORT.json"

    private struct Failure: Encodable {
        let succeeded = false
        let reason = "invalid_command"
        let detail: String
    }

    static func main() async {
        let arguments = Array(CommandLine.arguments.dropFirst())
        if arguments == ["--help"] {
            print(usage)
            return
        }
        guard let command = arguments.first,
              ["inspect", "evaluate", "precision"].contains(command) else {
            fail("Expected inspect, evaluate or precision. " + usage)
        }
        let options = Array(arguments.dropFirst())
        if options == ["--help"] {
            print("Usage: osdi \(command) --request REQUEST.json --output REPORT.json")
            return
        }
        let succeeded: Bool
        switch command {
        case "inspect": succeeded = await OSDIInspectCommand.run(arguments: options)
        case "evaluate": succeeded = await OSDIEvaluateCommand.run(arguments: options)
        default:
            guard options.count == 4, options[0] == "--request", options[2] == "--output" else {
                fail("Expected --request PATH --output PATH for precision")
            }
            succeeded = await OSDIEvaluatePrecisionCommand.run(requestPath: options[1], outputPath: options[3])
        }
        if !succeeded { exit(1) }
    }

    private static func fail(_ detail: String) -> Never {
        do {
            let data = try JSONEncoder().encode(Failure(detail: detail))
            FileHandle.standardError.write(data)
            FileHandle.standardError.write(Data([10]))
        } catch {
            FileHandle.standardError.write(Data("OSDI command failure report could not be encoded: \(error)\n".utf8))
        }
        exit(1)
    }
}
