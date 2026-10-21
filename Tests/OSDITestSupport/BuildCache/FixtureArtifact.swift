import Foundation

public struct FixtureArtifact: Sendable {
    public let file: URL
    public let sha256: String
    public let wasBuilt: Bool
    public let executable: Bool

    public func materialize(to destination: URL) throws -> URL {
        try FileManager.default.copyItem(at: file, to: destination)
        do {
            // A run-local copy owns its permissions and may be mutated by negative tests.
            try FileManager.default.setAttributes([.posixPermissions: executable ? 0o700 : 0o600], ofItemAtPath: destination.path)
            guard try FixtureFileIdentity.digest(destination, maximumBytes: 16_777_216) == sha256 else {
                throw FixtureBuildError.changedInput(file.path)
            }
            return destination
        } catch {
            let primary = error
            do { try FileManager.default.removeItem(at: destination) }
            catch { throw FixtureBuildError.cleanup(primary: primary, secondary: error) }
            throw primary
        }
    }
}
