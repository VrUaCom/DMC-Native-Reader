import Foundation
import SwiftUI
import UniformTypeIdentifiers

/// Content types offered by the document pickers. Declared in Info.plist; `data`
/// keeps resources under unexpected extensions pickable, since the native probe
/// decides by content and fails closed either way.
enum DmcContentType {
    static let scm = UTType(exportedAs: "com.dmcrengine.scm")
    static let mod = UTType(exportedAs: "com.dmcrengine.mod")
    static let ptx = UTType(exportedAs: "com.dmcrengine.ptx")
    static let evt = UTType(exportedAs: "com.dmcrengine.evt")
    static let tm2 = UTType(exportedAs: "com.dmcrengine.tm2")
    static let dds = UTType(importedAs: "com.microsoft.dds")

    static var resources: [UTType] { [scm, mod, ptx, dds, tm2, evt, .data] }
    static var textures: [UTType] { [ptx, dds, tm2, .data] }
}

/// App state: a navigation stack of native sessions plus the renderer's view
/// state. Every capability decision is read from the session (Black Widow),
/// never inferred here from names or extensions.
@MainActor
final class ResourceStore: ObservableObject {
    /// Root resource first; opened children are pushed on top.
    @Published private(set) var stack: [DmcResource] = []
    @Published var errorMessage: String?
    @Published var notice: String?
    /// Bumped when the current session changes in place (texture attached),
    /// so views re-read its capabilities and re-render.
    @Published private(set) var revision = 0

    @Published var yaw: Float = 0.65
    @Published var pitch: Float = -0.45
    @Published var zoom: Float = 1.0
    @Published var wireframe = false

    var current: DmcResource? { stack.last }
    var canGoBack: Bool { stack.count > 1 }

    func open(url: URL) {
        do {
            let resource = try DmcResource(contentsOf: url)
            stack = [resource]
            resetView()
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    func openChild(at index: Int) {
        guard let parent = current else { return }
        if let child = parent.openChild(at: index) {
            stack.append(child)
            resetView()
        } else {
            errorMessage = "This entry could not be opened."
        }
    }

    func goBack() {
        guard canGoBack else { return }
        stack.removeLast()
        resetView()
    }

    func attachTexture(url: URL) {
        guard let resource = current else { return }
        do {
            try resource.attachTexture(from: url)
            notice = resource.textureAttachmentDetail
        } catch {
            errorMessage = error.localizedDescription
        }
        revision += 1
    }

    func resetView() {
        yaw = 0.65
        pitch = -0.45
        zoom = 1.0
    }
}
