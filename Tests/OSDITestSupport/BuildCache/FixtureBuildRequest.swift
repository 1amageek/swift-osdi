import Foundation

public struct FixtureBuildRequest: Sendable {
    let compiler: FixtureCompiler
    let source: URL
    let dependencies: [URL]
    let arguments: [String]
    let fileName: String
    let executable: Bool

    public init(compiler: FixtureCompiler, source: URL, dependencies: [URL], arguments: [String],
                fileName: String, executable: Bool = false) {
        self.compiler = compiler
        self.source = source
        self.dependencies = dependencies
        self.arguments = arguments
        self.fileName = fileName
        self.executable = executable
    }
}
