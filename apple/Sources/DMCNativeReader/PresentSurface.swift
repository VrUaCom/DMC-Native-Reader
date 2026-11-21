// The view's Metal layer: the core draws frames straight onto it
// (dmc_view.window, DMC_RENDER_SHOWN) instead of handing their pixels back.
// Whether a frame goes there is the core's call; the shell only hands the
// layer over and shows it while the last frame was drawn on it.

import AppKit
import CDMCReader
import QuartzCore
import SwiftUI

final class PresentHostView: NSView {
    let metalLayer = CAMetalLayer()

    override init(frame: NSRect) {
        super.init(frame: frame)
        // Layer-hosting: AppKit leaves the layer's contents to the GPU.
        metalLayer.contentsGravity = .resizeAspect
        metalLayer.isOpaque = false
        metalLayer.backgroundColor = NSColor.clear.cgColor
        metalLayer.autoresizingMask = [.layerWidthSizable, .layerHeightSizable]
        layer = metalLayer
        wantsLayer = true
    }

    required init?(coder: NSCoder) { nil }
}

struct PresentSurface: NSViewRepresentable {
    let model: ViewerModel

    func makeNSView(context: Context) -> PresentHostView {
        let view = PresentHostView()
        model.presentLayer = view.metalLayer
        return view
    }

    func updateNSView(_ nsView: PresentHostView, context: Context) {
        if model.presentLayer !== nsView.metalLayer { model.presentLayer = nsView.metalLayer }
    }

    static func dismantleNSView(_ nsView: PresentHostView, coordinator: ()) {
        dmc_gpu_release(Unmanaged.passUnretained(nsView.metalLayer).toOpaque())
    }
}
