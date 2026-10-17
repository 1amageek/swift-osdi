import Foundation

/// Fresh native inputs and lifetime markers retained through all cache users.
struct OSDIFixtureSet: Sendable {
    let directory: URL
    let manifest: OSDIFixtureManifest

    func library(_ name: String) throws -> URL {
        guard let library = manifest.libraries[name] else {
            throw OSDIFixtureError.missingFixture(name)
        }
        return URL(fileURLWithPath: library.path)
    }

    func model(_ name: String) throws -> URL {
        guard let model = manifest.models[name] else {
            throw OSDIFixtureError.missingFixture(name)
        }
        return URL(fileURLWithPath: model.path)
    }

    func lifetime(_ name: String) throws -> [String] {
        guard let library = manifest.libraries[name] else {
            throw OSDIFixtureError.missingFixture(name)
        }
        guard FileManager.default.fileExists(atPath: library.lifetimeMarker) else { return [] }
        return try String(contentsOfFile: library.lifetimeMarker, encoding: .utf8)
            .split(separator: "\n").map(String.init)
    }
}
