/// Errors surfaced by the Ecritum Swift wrapper.
public enum EcritumError: Error, Equatable, Sendable {
    /// No local or release runtime artifact was resolved by Package.swift.
    case runtimeArtifactMissing

    /// A C ABI call returned a non-zero status.
    case runtimeCallFailed(status: Int32)
}

