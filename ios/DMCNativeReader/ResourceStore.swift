import Foundation
import SwiftUI
import UniformTypeIdentifiers

// Stable v1 families. Generic .data remains pickable, but the C++20 probe
// still fails closed if the bytes do not belong to a promoted module.
enum DmcContentType {
    static let mod = UTType(exportedAs: "com.dmcrengine.resource.mod")
    static let scm = UTType(exportedAs: "com.dmcrengine.resource.scm")
    static let dds = UTType(exportedAs: "com.dmcrengine.resource.dds")
    static let ptx = UTType(exportedAs: "com.dmcrengine.resource.ptx")

    static let pickable: [UTType] = [mod, scm, dds, ptx, .data]
}

@MainActor
final class ResourceStore: ObservableObject {
    @Published private(set) var resource: DmcResource?
    @Published private(set) var fileName = ""
    @Published private(set) var status = "No resource open."
    @Published var errorMessage: String?

    @Published var yaw: Float = 0.65
    @Published var pitch: Float = -0.45
    @Published var zoom: Float = 1.0
    @Published var wireframe = false
    @Published var hierarchy = false

    var hasResource: Bool { resource != nil }
    var hasGeometry: Bool { resource?.hasGeometry ?? false }
    var canShowHierarchy: Bool { resource?.hierarchyAvailable ?? false }

    // Keep the bridge generic. Bit 0 and bit 1 are the shared C++20
    // RenderFlag::Wireframe and RenderFlag::Hierarchy contract.
    var renderFlags: DmcRenderFlags {
        var raw: UInt = 0
        if wireframe { raw |= 1 << 0 }
        if hierarchy && canShowHierarchy { raw |= 1 << 1 }
        return DmcRenderFlags(rawValue: raw)
    }

    func open(url: URL) {
        fileName = url.lastPathComponent
        do {
            let decoded = try DmcResource(contentsOf: url)
            resource = decoded
            status = decoded.summary
            errorMessage = nil
            wireframe = false
            hierarchy = false
            resetView()
        } catch {
            resource = nil
            status = "\(fileName)\nOpen failed: \(error.localizedDescription)"
            errorMessage = error.localizedDescription
            wireframe = false
            hierarchy = false
        }
    }

    func resetView() {
        yaw = 0.65
        pitch = -0.45
        zoom = 1.0
    }

    func toggleWireframe() {
        guard hasGeometry else { return }
        wireframe.toggle()
    }

    func toggleHierarchy() {
        guard canShowHierarchy else {
            hierarchy = false
            return
        }
        hierarchy.toggle()
    }
}
