import COSDI
import CircuiteFoundation
import Foundation
import OSDIContracts

/// Loads an existing native OSDI image. This does not invoke a compiler or model evaluator.
public struct OSDINativeLibraryLoader: OSDILibraryLoading {
    public let stagingParentDirectory: URL

    public init(stagingParentDirectory: URL = FileManager.default.temporaryDirectory) {
        self.stagingParentDirectory = stagingParentDirectory
    }

    public func load(_ intent: ArtifactAccessIntent, source: any ArtifactAccessing,
                     limits: OSDILoadLimits) async throws(OSDIError) -> OSDILibrary {
        let directory = try await OSDIArtifactMaterializer(parent: stagingParentDirectory)
            .materialize(intent, source: source)
        if Task.isCancelled {
            try Self.failRemoving(directory, primary: .cancelled)
        }
        var nativeLimits = cs_osdi_limits(maximum_descriptors: limits.maximumDescriptors,
            maximum_metadata_bytes: limits.maximumMetadataBytes,
            maximum_storage_bytes: limits.maximumStorageBytes)
        var handle: OpaquePointer?
        var issue = cs_osdi_error()
        let status = directory.appendingPathComponent("model.osdi").path.withCString {
            cs_osdi_open($0, &nativeLimits, &handle, &issue)
        }
        if status == CS_OSDI_CLOSE_FAILED, let handle {
            let primary = OSDIError.native(NativeIssueBridge.issue(issue))
            let secondary = OSDIError.native(NativeIssueBridge.cleanupIssue(issue))
            throw .pendingCleanup(primary: primary, secondary: secondary,
                owner: OSDIPendingLibraryCleanup(handle: handle, stagingDirectory: directory))
        }
        guard status == CS_OSDI_SUCCESS else {
            try Self.failRemoving(directory, primary: .native(NativeIssueBridge.issue(issue)))
        }
        guard let handle else { try Self.failRemoving(directory, primary: .invalidRange) }
        // Ownership transfers on entry, including publication failure and retryable cleanup.
        return try OSDILibraryOwner.publish(handle: handle, stagingDirectory: directory,
            reference: intent.expectedReference, limits: limits)
    }

    private static func failRemoving(_ directory: URL, primary: OSDIError) throws(OSDIError) -> Never {
        do { try FileManager.default.removeItem(at: directory) }
        catch { throw .cleanup(primary: primary,
            secondary: .host(operation: "removePrivateDirectory", underlying: error)) }
        throw primary
    }
}
