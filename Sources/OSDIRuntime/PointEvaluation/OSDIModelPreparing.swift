import OSDIContracts

public protocol OSDIModelPreparing: Sendable {
    func prepare(library: borrowing OSDILibrary, descriptorIndex: Int,
                 options: borrowing OSDIPreparationOptions) throws(OSDIError) -> OSDIPreparedModel
}
