/// Scoped conditional actual-source formula bounds for one terminal leaf.
public struct OSDIPrecisionDecisionCoverLeafView: ~Escapable {
    public let fLower: Span<Double>
    public let fUpper: Span<Double>
    public let qLower: Span<Double>
    public let qUpper: Span<Double>
    public let gLower: Span<Double>
    public let gUpper: Span<Double>
    public let cLower: Span<Double>
    public let cUpper: Span<Double>
    @_lifetime(copy storage)
    internal init(storage: Span<Double>, nodes: Int) {
        let matrix = nodes * nodes
        fLower = storage.extracting(0..<nodes); fUpper = storage.extracting(nodes..<2*nodes)
        qLower = storage.extracting(2*nodes..<3*nodes); qUpper = storage.extracting(3*nodes..<4*nodes)
        gLower = storage.extracting(4*nodes..<4*nodes+matrix); gUpper = storage.extracting(4*nodes+matrix..<4*nodes+2*matrix)
        cLower = storage.extracting(4*nodes+2*matrix..<4*nodes+3*matrix); cUpper = storage.extracting(4*nodes+3*matrix..<4*nodes+4*matrix)
    }
}
