import CircuiteFoundation
import CircuiteFoundationFileSystem
import CircuiteFoundationFoundation
import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

/// Preparation-only I/O. The exact staged file is retained by the native library owner.
internal struct OSDIArtifactMaterializer {
    let parent: URL

    func materialize(_ intent: ArtifactAccessIntent, source: any ArtifactAccessing)
        async throws(OSDIError) -> URL {
        guard parent.isFileURL, parent.path.hasPrefix("/"),
              intent.operation == .read, intent.expectedReference.byteCount > 0,
              intent.expectedReference.byteCount <= intent.budget.maximumTotalByteCount else {
            throw .invalidRange
        }
        guard intent.expectedReference.digest.algorithm == .sha256 else { throw .unsupportedDigest }
        guard !Task.isCancelled else { throw .cancelled }
        let directory = parent.appendingPathComponent("osdi-" + UUID().uuidString, isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false,
                attributes: [.posixPermissions: 0o700])
        } catch { throw .host(operation: "createPrivateDirectory", underlying: error) }
        do {
            try await copy(intent, source: source, to: directory.appendingPathComponent("model.osdi"))
            return directory
        } catch {
            let primary = Self.issue(error, operation: "materialize")
            do { try FileManager.default.removeItem(at: directory) }
            catch { throw OSDIError.cleanup(primary: primary,
                secondary: .host(operation: "removePrivateDirectory", underlying: error)) }
            throw primary
        }
    }

    private func copy(_ intent: ArtifactAccessIntent, source: any ArtifactAccessing, to output: URL)
        async throws(OSDIError) {
        let session: any ArtifactReadSession
        do { session = try await source.open(intent) }
        catch { throw .artifact(error) }
        var descriptor: Int32 = -1
        var failure: OSDIError?
        var offset: UInt64 = 0
        var pages: UInt64 = 0
        do {
            descriptor = output.path.withCString { open($0, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0o600) }
            guard descriptor >= 0 else { throw OSDIError.io(operation: "openStagedFile", code: errno) }
            while true {
                guard !Task.isCancelled else { throw OSDIError.cancelled }
                let request = try ArtifactReadPageRequest(offset: offset,
                    maximumByteCount: intent.budget.maximumPageByteCount)
                guard pages < intent.budget.maximumPageCount else { throw OSDIError.resourceLimit }
                pages += 1
                let page = try await session.readPage(request)
                let (end, overflow) = offset.addingReportingOverflow(page.bytes.byteCount)
                guard !overflow, page.offset == offset,
                      page.bytes.byteCount <= request.maximumByteCount,
                      end <= intent.expectedReference.byteCount else { throw OSDIError.identityMismatch }
                try page.bytes.withUnsafeBytes { bytes in
                    guard UInt64(bytes.count) == page.bytes.byteCount else { throw OSDIError.identityMismatch }
                    try Self.write(bytes, to: descriptor)
                }
                offset = end
                if page.completion == .complete {
                    guard offset == intent.expectedReference.byteCount,
                          let receipt = page.finalReceipt,
                          receipt.observedArtifactID == intent.expectedReference.id,
                          receipt.totalByteCount == offset else { throw OSDIError.identityMismatch }
                    break
                }
                guard page.bytes.byteCount > 0 else { throw OSDIError.identityMismatch }
            }
        } catch { failure = Self.issue(error, operation: "readAndWriteArtifact") }
        if descriptor >= 0, close(descriptor) != 0 {
            failure = Self.combine(failure, .io(operation: "closeStagedFile", code: errno))
        }
        let termination = await session.close()
        do {
            let receipt = try await termination.wait()
            if failure == nil && (!receipt.didReachTerminalPage || receipt.sessionIdentity != session.identity) {
                failure = .identityMismatch
            }
        }
        catch { failure = Self.combine(failure, .artifactClose(error)) }
        if let failure { throw failure }
        guard !Task.isCancelled else { throw .cancelled }
        guard output.path.withCString({ chmod($0, 0o500) }) == 0 else {
            throw .io(operation: "protectStagedFile", code: errno)
        }
        do {
            let descriptor = intent.expectedReference.descriptor
            let locator = ArtifactLocator(location: try ArtifactLocation(fileURL: output),
                role: descriptor.role, kind: descriptor.kind, format: descriptor.format)
            let actual = try LocalArtifactReferencer().reference(locator)
            guard actual.id == intent.expectedReference.id else { throw OSDIError.identityMismatch }
        } catch { throw Self.issue(error, operation: "verifyStagedArtifact") }
    }

    private static func write(_ bytes: UnsafeRawBufferPointer, to descriptor: Int32) throws(OSDIError) {
        // The page owns initialized bytes through this closure. No pointer is retained or crosses await.
        // Offset is bounded by bytes.count, so each advance is within the original raw byte range.
        var written = 0
        while written < bytes.count {
            guard let base = bytes.baseAddress else { throw .invalidRange }
            #if canImport(Darwin)
            let result = Darwin.write(descriptor, base.advanced(by: written), bytes.count - written)
            #else
            let result = Glibc.write(descriptor, base.advanced(by: written), bytes.count - written)
            #endif
            if result < 0, errno == EINTR { continue }
            guard result > 0 else { throw .io(operation: "writeStagedFile", code: result < 0 ? errno : EIO) }
            written += result
        }
    }

    private static func issue(_ error: any Error, operation: String) -> OSDIError {
        if let error = error as? OSDIError { return error }
        if let error = error as? ArtifactAccessError { return .artifact(error) }
        return .host(operation: operation, underlying: error)
    }

    private static func combine(_ primary: OSDIError?, _ secondary: OSDIError) -> OSDIError {
        if let primary { return .cleanup(primary: primary, secondary: secondary) }
        return secondary
    }
}
