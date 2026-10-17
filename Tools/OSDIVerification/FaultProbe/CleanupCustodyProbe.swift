import COSDI
import CircuiteFoundation
import Foundation
import FakeControl
import OSDIContracts

@main struct Probe {
    static func initialHelper(_ directory: URL, delegated: Bool) throws {
        if delegated {
            let library = try OSDILibraryOwner.publish(handle: OpaquePointer(fake_handle()!),
                stagingDirectory: directory, reference: try ArtifactReference(
                    digest: ContentDigest(algorithm: .sha256, hexadecimalValue: String(repeating: "0", count: 64)),
                    byteCount: 0, descriptor: ArtifactDescriptor(role: .input, kind: .model, format: .unknown)),
                limits: try OSDILoadLimits(maximumDescriptors: 1, maximumMetadataBytes: 1000,
                    maximumStorageBytes: 1000))
            try library.close()
        } else {
            let pending = OSDIPendingLibraryCleanup(handle: OpaquePointer(fake_handle()!),
                stagingDirectory: directory)
            try pending.close()
        }
    }

    static func retryHelper(_ directory: URL, delegated: Bool) throws {
        do {
            try initialHelper(directory, delegated: delegated)
            preconditionFailure("Injected initial failure was lost")
        } catch OSDIError.cleanupRequired(let cause, let pending) {
            guard case .native(let issue) = cause else { preconditionFailure("Primary cause changed") }
            precondition(issue.code == 9)
            precondition(fake_close_count() == 1)
            try pending.close()
        }
    }

    static func main() throws {
        for delegated in [false, true] {
            fake_reset()
            let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: false)
            do {
                try retryHelper(directory, delegated: delegated)
                preconditionFailure("Injected retry failure was lost")
            } catch OSDIError.cleanupRequired(let cause, let pending) {
                guard case .native(let issue) = cause else { preconditionFailure("Retry cause changed") }
                precondition(issue.code == 9)
                precondition(fake_close_count() == 2, "Helper unwinding retried deinit cleanup")
                do {
                    try pending.close()
                    preconditionFailure("Persistent failure stopped propagating")
                } catch OSDIError.cleanupRequired(let retryCause, let retryOwner) {
                    guard case .native(let retry) = retryCause else { preconditionFailure("Repeated cause changed") }
                    precondition(retry.code == 9)
                    precondition(retryOwner === pending, "Retry changed cleanup custody")
                }
                precondition(fake_close_count() == 3)
                fake_allow_close()
                try pending.close()
                precondition(fake_close_count() == 4)
                precondition(!FileManager.default.fileExists(atPath: directory.path))
                print("PASS delegated=\(delegated): nested helper retry reached outer catch; same owner survived; cleanup succeeded")
            }
        }
    }
}
