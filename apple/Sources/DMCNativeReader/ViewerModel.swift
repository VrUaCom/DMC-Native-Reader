// The viewer's state: the open session and the way back (a PAC's files),
// the camera, and the picture on the screen. Frames are drawn by the core on
// a background queue; only the newest request is drawn.
import AppKit
import CoreGraphics
import Foundation

@MainActor
final class ViewerModel: ObservableObject {
    @Published private(set) var session: ReaderSession?
    @Published private(set) var image: CGImage?
    @Published private(set) var isPicture = false
    @Published private(set) var healthLevel = 0
    @Published private(set) var message: String?
    @Published var camera = ViewCamera() { didSet { requestFrame() } }

    /// Sessions under the current one (Back returns to the last).
    private(set) var stack: [ReaderSession] = []
    private let settings = ReaderCore.applyDefaultSettings()
    private let renderQueue = DispatchQueue(label: "dmc.render", qos: .userInitiated)
    private var viewSize = CGSize(width: 800, height: 600)
    private var scale: CGFloat = 2
    private var generation = 0
    private var drawing = false
    private var pending = false

    let welcome = ReaderCore.welcomeText(
        version: Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev")

    var title: String { session?.title ?? "DMC Native Reader" }
    var canGoBack: Bool { !stack.isEmpty }
    /// The files inside the open session (a PAC's slots, a PTX's textures).
    @Published private(set) var children: [ChildFile] = []

    // MARK: Opening

    func open(url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        let data: Data
        do {
            data = try Data(contentsOf: url, options: .mappedIfSafe)
        } catch {
            message = "Could not read \(url.lastPathComponent): \(error.localizedDescription)"
            return
        }
        switch ReaderSession.open(name: url.lastPathComponent, bytes: data) {
        case .success(let opened):
            stack.removeAll()
            show(opened)
        case .failure(let failure):
            message = failure.message
        }
    }

    func openChild(_ index: Int) {
        guard let session, let child = session.openChild(index) else {
            message = "This file could not be opened."
            return
        }
        stack.append(session)
        show(child)
    }

    func goBack() {
        guard let previous = stack.popLast() else { return }
        show(previous)
    }

    private func show(_ next: ReaderSession) {
        session = next
        message = nil
        camera = ViewCamera()
        healthLevel = next.health().level
        children = (0..<next.childCount).map { ChildFile(index: $0, title: next.childTitle($0)) }
        if let picture = next.picture() {
            isPicture = true
            image = Self.cgImage(picture)
        } else {
            isPicture = false
            image = nil
            requestFrame()
        }
    }

    // MARK: Info

    func infoText() -> String {
        guard let session else { return welcome }
        return session.infoText(navigationDepth: stack.count, backTitle: stack.last?.title ?? "")
    }

    func healthReport() -> String { session?.health().report ?? "" }

    // MARK: Frames

    func viewChanged(size: CGSize, scale: CGFloat) {
        guard size != viewSize || scale != self.scale else { return }
        viewSize = size
        self.scale = scale
        requestFrame()
    }

    func resetCamera() { camera = ViewCamera() }

    private func requestFrame() {
        guard let session, !isPicture else { return }
        if drawing {
            pending = true
            return
        }
        drawing = true
        generation += 1
        let request = generation
        let (width, height) = frameSize()
        var flags = settings.renderFlags
        if session.state.contains(.uvMapView) {
            flags = RenderFlag.uvLayout
        } else if settings.shadows {
            flags |= RenderFlag.shadows
        }
        let camera = self.camera
        renderQueue.async { [weak self] in
            let frame = session.render(width: width, height: height, camera: camera, flags: flags)
            let picture = frame.flatMap(Self.cgImage)
            Task { @MainActor in
                guard let self else { return }
                self.drawing = false
                if request == self.generation, self.session === session {
                    if let picture { self.image = picture }
                    if frame == nil, self.image == nil {
                        self.message = self.children.isEmpty
                            ? "Nothing to draw in this file: see Info."
                            : "Nothing to draw: its files are listed on the left."
                    }
                }
                if self.pending {
                    self.pending = false
                    self.requestFrame()
                }
            }
        }
    }

    /// Nothing to draw and no picture: the session is only its files or its Info.
    var drawsNothing: Bool { session != nil && image == nil && message != nil }

    /// The view's size in pixels, the longest side capped by the settings.
    private func frameSize() -> (Int, Int) {
        var width = max(1, viewSize.width * scale)
        var height = max(1, viewSize.height * scale)
        let longest = max(width, height)
        let cap = CGFloat(max(360, settings.maxSide))
        if longest > cap {
            width *= cap / longest
            height *= cap / longest
        }
        return (Int(width.rounded()), Int(height.rounded()))
    }

    nonisolated private static func cgImage(_ image: RGBAImage) -> CGImage? {
        let data = Data(image.pixels) as CFData
        guard let provider = CGDataProvider(data: data) else { return nil }
        return CGImage(width: image.width, height: image.height, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: image.width * 4, space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
    }
}

struct ChildFile: Identifiable, Hashable {
    let index: Int
    let title: String
    var id: Int { index }
}
