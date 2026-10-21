import Foundation
import SignoffToolSupport

public struct FixtureCompiler: Sendable {
    public let executable: URL
    public let sdk: URL
    let identities: [FixtureFileIdentity]
    let environment: [String: String]

    public static func selected(executable override: URL? = nil) async throws -> Self {
        let inherited = ProcessInfo.processInfo.environment
        var environment = ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LANG": "C", "LC_ALL": "C"]
        for name in ["TOOLCHAINS", "DEVELOPER_DIR"] {
            if let value = inherited[name] { environment[name] = value }
        }
        let runner = TimedProcessRunner(timeoutSeconds: 10, maximumOutputByteCount: 262_144, cleanupTimeoutSeconds: 3)
        func resolve(_ arguments: [String]) async throws -> URL {
            let result = try await runner.run(executableURL: URL(fileURLWithPath: "/usr/bin/xcrun"),
                arguments: arguments, environment: environment)
            guard result.exitCode == 0 else {
                throw FixtureBuildError.commandFailed(result)
            }
            let path = result.standardOutput.trimmingCharacters(in: .whitespacesAndNewlines)
            guard path.hasPrefix("/") else { throw FixtureBuildError.invalidInput("xcrun returned a nonabsolute path") }
            return URL(fileURLWithPath: path).resolvingSymlinksInPath()
        }
        let backend = try await resolve(["--find", "clang"])
        let executable = (override ?? inherited["OSDI_TEST_CLANG"].map { URL(fileURLWithPath: $0) } ?? backend)
            .resolvingSymlinksInPath()
        guard FileManager.default.isExecutableFile(atPath: executable.path) else {
            throw FixtureBuildError.invalidInput("Clang must be an executable file: \(executable.path)")
        }
        let sdk: URL
        if let selected = inherited["SDKROOT"], selected.hasPrefix("/"), selected != "/" {
            sdk = URL(fileURLWithPath: selected).resolvingSymlinksInPath()
        } else { sdk = try await resolve(["--sdk", "macosx", "--show-sdk-path"]) }
        let settings = ["SDKSettings.json", "SDKSettings.plist"].map { sdk.appendingPathComponent($0) }
        guard let settings = settings.first(where: { FileManager.default.fileExists(atPath: $0.path) }) else {
            throw FixtureBuildError.invalidInput("SDK settings are missing: \(sdk.path)")
        }
        return try Self(executable: executable, sdk: sdk,
            identities: [FixtureFileIdentity(executable), FixtureFileIdentity(backend), FixtureFileIdentity(settings)],
            environment: environment)
    }

    public func arguments(architecture: String = "arm64") -> [String] {
        ["-isysroot", sdk.path, "-target", "\(architecture)-apple-macos26.0", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    }
}
