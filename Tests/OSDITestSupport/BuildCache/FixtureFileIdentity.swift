import CryptoKit
import Foundation

struct FixtureFileIdentity: Codable, Equatable, Sendable {
    let path: String
    let sha256: String

    init(_ file: URL) throws {
        let canonical = file.resolvingSymlinksInPath()
        guard try canonical.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile == true else {
            throw FixtureBuildError.invalidInput("A regular input file is required: \(file.path)")
        }
        path = canonical.path
        sha256 = try Self.digest(canonical)
    }

    static func digest(_ file: URL, maximumBytes: Int = 1_073_741_824) throws -> String {
        guard try file.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile == true else {
            throw FixtureBuildError.invalidInput("A regular digest input is required: \(file.path)")
        }
        let handle = try FileHandle(forReadingFrom: file)
        defer { handle.closeFile() }
        var hasher = SHA256(), total = 0
        while let data = try handle.read(upToCount: 262_144), !data.isEmpty {
            guard data.count <= maximumBytes - total else {
                throw FixtureBuildError.invalidInput("Input exceeds its byte limit: \(file.path)")
            }
            total += data.count
            hasher.update(data: data)
        }
        return hex(hasher.finalize())
    }

    static func digest(_ data: Data) -> String { hex(SHA256.hash(data: data)) }

    private static func hex(_ digest: SHA256.Digest) -> String {
        digest.map { String(format: "%02x", $0) }.joined()
    }
}
