import Ecritum
import Foundation

do {
    print("SwiftHost version=\(try Ecritum.version)")
} catch {
    fputs("SwiftHost failed: \(error)\n", stderr)
    exit(1)
}
