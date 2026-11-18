// The viewer's state: the open session and the way back (a PAC's files),
// the camera, and the picture on the screen. Frames are drawn by the core on
// a background queue; only the newest request is drawn.
import AppKit
import CDMCReader
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

    // MARK: Animation state
    /// The animation strip of the open session (its motions and scripts).
    @Published private(set) var motionCards: [MotionCard] = []
    /// The card whose motion is loaded, if any.
    @Published private(set) var activeCard: Int?
    @Published private(set) var playing = false
    /// The motion frame shown (60 per second), NaN: the pose as it is.
    private(set) var motionFrame: Float = .nan
    private var motionRange: (end: Float, loopStart: Float)?
    private var playTimer: Timer?
    private var lastTick = Date()
    /// The companion menu of the open session, written by the core.
    @Published private(set) var actions: [MenuRow] = []
    @Published private(set) var moreActions: [MenuRow] = []

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
        // The picture is known before the camera resets: resetting it asks
        // for a frame, and a picture session draws none.
        let picture = next.picture()
        isPicture = picture != nil
        image = picture.flatMap(Self.cgImage)
        session = next
        message = nil
        healthLevel = next.health().level
        children = (0..<next.childCount).map { ChildFile(index: $0, title: next.childTitle($0)) }
        stopMotion()
        motionFrame = .nan
        motionRange = nil
        activeCard = nil
        motionCards = isPicture ? [] : next.motionCards()
        let menu = next.companionRows()
        actions = menu.rows
        moreActions = menu.more
        camera = startCamera(of: next)  // draws the first frame of a 3D session
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

    func resetCamera() {
        guard let session else { camera = ViewCamera(); return }
        camera = startCamera(of: session)
    }

    /// A model's start view, or a stage's (the game's camera at its first spawn point).
    private func startCamera(of session: ReaderSession) -> ViewCamera {
        var start = ViewCamera()
        if let stage = session.stageStartView() {
            start.yaw = stage.yaw
            start.pitch = stage.pitch
            start.zoom = stage.zoom
        }
        return start
    }

    // MARK: Animations

    /// Loads a card's motion (the library's, or driven by one of its scripts) and plays it.
    func play(card: MotionCard, script: MotionCard.Script? = nil) {
        guard let session, card.libraryIndex >= 0 else { return }
        let report = script.map { session.loadScriptedMotion(script: $0.scriptIndex, motion: card.libraryIndex) }
            ?? session.loadLibraryMotion(card.libraryIndex)
        if !report.isEmpty { message = report }
        motionRange = session.motionRange()
        activeCard = card.id
        motionFrame = motionRange == nil ? .nan : 0
        if motionRange != nil { startMotion() } else { requestFrame() }
    }

    /// A motion file the user picked onto the model.
    func loadMotion(url: URL) {
        guard let session else { return }
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
            message = "Could not read \(url.lastPathComponent)."
            return
        }
        message = session.loadMotion(name: url.lastPathComponent, bytes: data)
        motionRange = session.motionRange()
        motionCards = session.motionCards()
        motionFrame = motionRange == nil ? .nan : 0
        if motionRange != nil { startMotion() } else { requestFrame() }
    }

    func togglePlaying() {
        if playing { stopMotion() } else if motionRange != nil { startMotion() }
    }

    private func startMotion() {
        stopMotion()
        playing = true
        lastTick = Date()
        playTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
    }

    private func stopMotion() {
        playTimer?.invalidate()
        playTimer = nil
        playing = false
    }

    /// One step of the playing motion: 60 game frames per second, looping like Android.
    private func tick() {
        guard playing, let range = motionRange else { return }
        let now = Date()
        let dt = Float(min(0.1, now.timeIntervalSince(lastTick)))
        lastTick = now
        var frame = (motionFrame.isFinite ? motionFrame : 0) + dt * 60
        if range.end > 0, frame > range.end {
            let span = range.end - range.loopStart
            frame = span > 0 ? range.loopStart + (frame - range.end).truncatingRemainder(dividingBy: span) : range.end
        }
        motionFrame = frame
        requestFrame()
    }

    // MARK: Actions

    /// A companion menu row chosen: the core runs it and says what is left to do.
    func run(action: MenuRow) {
        guard let session else { return }
        let outcome = session.runCompanionAction(action.id)
        if !outcome.message.isEmpty { message = outcome.message }
        if let opened = outcome.opened {
            stack.append(session)
            show(opened)
            return
        }
        if outcome.step == Int32(DMC_STEP_RESET_VIEW.rawValue) { resetCamera() }
        // The session may have changed (a part attached, a texture re-encoded): refresh what depends on it.
        let menu = session.companionRows()
        actions = menu.rows
        moreActions = menu.more
        motionCards = session.motionCards()
        requestFrame()
    }

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
        let motionFrame = self.motionFrame
        renderQueue.async { [weak self] in
            let frame = session.render(width: width, height: height, camera: camera, flags: flags,
                                       motionFrame: motionFrame)
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

    /// The view's size in pixels, the longest side capped by the settings and
    /// each side within what the core draws (a thin view is scaled up from it).
    private func frameSize() -> (Int, Int) {
        var width = max(1, viewSize.width * scale)
        var height = max(1, viewSize.height * scale)
        let longest = max(width, height)
        let cap = CGFloat(max(360, settings.maxSide))
        if longest > cap {
            width *= cap / longest
            height *= cap / longest
        }
        let side = { (value: CGFloat) in
            min(Int(DMC_RENDER_MAX_SIDE), max(Int(DMC_RENDER_MIN_SIDE), Int(value.rounded())))
        }
        return (side(width), side(height))
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
