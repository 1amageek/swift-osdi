import COSDI
import OSDIContracts

/// Prepares the selected generated model once, preserving its parameter and topology semantics.
public struct OSDINativeModelPreparer: OSDIModelPreparing {
    public init() {}

    public func prepare(library: borrowing OSDILibrary, descriptorIndex: Int,
                        options: borrowing OSDIPreparationOptions) throws(OSDIError) -> OSDIPreparedModel {
        guard library.descriptors.indices.contains(descriptorIndex) else { throw .invalidRange }
        let descriptor = library.descriptors[descriptorIndex]
        let lease = try library.owner.acquireLease(descriptor: descriptor, evaluation: true)
        let bridge = try OSDIPreparationBridge(options: options)
        var evaluator: OpaquePointer?
        var error = cs_osdi_error()
        let status = try lease.withNativeLibrary { handle in
            bridge.withOptions { nativeOptions in
                cs_osdi_prepare(handle, UInt32(descriptorIndex), nativeOptions, &evaluator, &error)
            }
        }
        guard status == 0 else {
            if let evaluator { cs_osdi_evaluator_destroy(evaluator) }
            throw .native(NativeIssueBridge.issue(error))
        }
        guard let evaluator else { throw .invalidRange }
        do {
            let snapshot = try lease.withNativeLibrary { handle throws(OSDIError) in
                try OSDIMetadataSnapshot(library: handle, evaluator: evaluator, descriptor: descriptor, limits: options.limits)
            }
            return OSDIPreparedModel(handle: evaluator, lease: consume lease, topology: snapshot.topology,
                parameterMetadata: snapshot.parameters, resolvedParameters: snapshot.resolved,
                diagnostics: snapshot.diagnostics, limits: options.limits)
        } catch {
            cs_osdi_evaluator_destroy(evaluator)
            throw error
        }
    }
}
