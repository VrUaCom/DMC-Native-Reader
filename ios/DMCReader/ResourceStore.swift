import Foundation
import SwiftUI
import UniformTypeIdentifiers

/// Content types the document picker offers.
///
/// The custom types are declared in Info.plist as exported UTIs, so the Files
/// app can hand DMC resources to this app directly. `data` and `plainText` are
/// included so a resource stored under an unexpected extension is still
/// pickable — the native probe stays fail-closed either way.
enum DmcContentType {
    static let scm = UTType(exportedAs: "com.dmcrengine.scm")
    static let mod = UTType(exportedAs: "com.dmcrengine.mod")
    static let hits = UTType(exportedAs: "com.dmcrengine.hits")
    static let index = UTType(exportedAs: "com.dmcrengine.index")
    static let ukn = UTType(exportedAs: "com.dmcrengine.ukn")

    static var pickable: [UTType] {
        [scm, mod, hits, index, ukn, .plainText, .data]
    }
}

/// Holds the decoded resource and the view state shared by the content views.
@MainActor
final class ResourceStore: ObservableObject {
    @Published private(set) var resource: DmcResource?
    @Published private(set) var fileName: String = ""
    @Published private(set) var status: String = "No resource open."
    @Published var errorMessage: String?

    // View state for the CPU renderer.
    @Published var yaw: Float = 0.65
    @Published var pitch: Float = -0.45
    @Published var zoom: Float = 1.0
    @Published var wireframe = false

    var isText: Bool { resource?.isText ?? false }
    var hasResource: Bool { resource != nil }

    func open(url: URL) {
        fileName = url.lastPathComponent
        do {
            let decoded = try DmcResource(contentsOf: url)
            resource = decoded
            status = decoded.summary
            resetView()
        } catch {
            resource = nil
            status = "\(fileName)\nOpen failed: \(error.localizedDescription)"
            errorMessage = error.localizedDescription
        }
    }

    func resetView() {
        yaw = 0.65
        pitch = -0.45
        zoom = 1.0
    }

    func toggleWireframe() {
        wireframe.toggle()
    }
}
