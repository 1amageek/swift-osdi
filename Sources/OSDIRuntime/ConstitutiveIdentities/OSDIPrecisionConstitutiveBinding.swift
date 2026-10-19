import OSDIContracts

/// Source identity and the exact correlated-coordinate condition of its claims.
public struct OSDIPrecisionConstitutiveBinding: Sendable, Equatable {
    public let source: OSDIPrecisionChartBinding
    public let coordinateQuotient: OSDIPrecisionCoordinateQuotient
    internal init(source: OSDIPrecisionChartBinding, coordinateQuotient: OSDIPrecisionCoordinateQuotient) {
        self.source = source; self.coordinateQuotient = coordinateQuotient
    }
}
