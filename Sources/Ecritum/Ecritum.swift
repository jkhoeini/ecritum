import CEcritum

/// Public entry point for the Ecritum Swift wrapper.
public enum Ecritum {
    /// True when Package.swift resolved a local or release runtime artifact.
    public static var runtimeArtifactAvailable: Bool {
        #if ECRITUM_HAS_RUNTIME_ARTIFACT
        true
        #else
        false
        #endif
    }

    /// The loaded Ecritum runtime version.
    public static var version: String {
        get throws {
            #if ECRITUM_HAS_RUNTIME_ARTIFACT
            let buffer = UnsafeMutablePointer<CChar>.allocate(capacity: Int(ECRITUM_VERSION_BUFFER_SIZE))
            defer { buffer.deallocate() }

            let status = ecritum_version(buffer, Int(ECRITUM_VERSION_BUFFER_SIZE))
            guard status == ECRITUM_OK else {
                let rawStatus = Int32(status)
                throw EcritumError.from(
                    status: rawStatus,
                    details: EcritumErrorDetails(
                        status: nil,
                        category: .unknown,
                        message: "Ecritum version lookup failed",
                        operation: "version"
                    )
                )
            }

            return String(cString: buffer)
            #else
            throw EcritumError.runtimeArtifactMissing
            #endif
        }
    }
}
