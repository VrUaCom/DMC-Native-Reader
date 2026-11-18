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
    /// The settings schema and the user's stored choices (UserDefaults), applied by the core.
    let settingRows = ReaderCore.settingsSchema()
    @Published private(set) var stored: [String: Float] =
        (UserDefaults.standard.dictionary(forKey: "settings") as? [String: Float]) ?? [:]
    private lazy var settings = ReaderCore.apply(stored: stored)

    /// The value a setting has now (stored, else the core's fallback).
    func value(of row: SettingRow) -> Float { stored[row.key] ?? row.fallback }

    /// A setting chosen: stored, applied by the core, and the view drawn again.
    func set(_ row: SettingRow, to value: Float) {
        stored[row.key] = value
        UserDefaults.standard.set(stored, forKey: "settings")
        settings = ReaderCore.apply(stored: stored)
        requestFrame()
    }
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
    /// A cutscene's sound and the subtitle shown now (both the core's).
    private let cutsceneAudio = CutsceneAudio()
    @Published private(set) var subtitle: Subtitle?
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
        subtitle = nil
        motionFrame = .nan
        motionRange = nil
        activeCard = nil
        motionCards = isPicture ? [] : next.motionCards()
        let menu = next.companionRows()
        actions = menu.rows
        moreActions = menu.more
        effects = isPicture ? [] : next.effectRows()
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
        if let session { cutsceneAudio.play(session, frame: motionFrame.isFinite ? motionFrame : 0) }
        playTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
    }

    private func stopMotion() {
        playTimer?.invalidate()
        playTimer = nil
        playing = false
        cutsceneAudio.stop()
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
        let looped = frame < motionFrame
        motionFrame = frame
        if looped, let session { cutsceneAudio.play(session, frame: frame) }  // the sound loops with the motion
        let shown = session?.subtitle()
        if shown != subtitle { subtitle = shown }
        requestFrame()
    }

    // MARK: Base room, effects, export

    @Published private(set) var roomName: String?
    @Published private(set) var effects: [MenuRow] = []

    func loadRoom(url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
            message = "Could not read \(url.lastPathComponent)."
            return
        }
        if let detail = ReaderCore.loadRoom(name: url.lastPathComponent, bytes: data) {
            roomName = url.lastPathComponent
            message = "Base room: \(url.lastPathComponent) — \(detail)"
        } else {
            message = "\(url.lastPathComponent) is not a room the core can draw."
        }
        refreshAfterChange()
    }

    func clearRoom() {
        ReaderCore.clearRoom()
        roomName = nil
        refreshAfterChange()
    }

    func playEffect(_ row: MenuRow) {
        guard let session else { return }
        let text = session.playEffect(row.id)
        if !text.isEmpty { message = text }
        requestFrame()
    }

    /// Saves the stage's export picture, or else the view as it is now, as PNG.
    func exportPNG(to url: URL) {
        let picture = session?.exportPicture().flatMap(Self.cgImage) ?? image
        guard let picture,
              let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else {
            message = "Nothing to export."
            return
        }
        CGImageDestinationAddImage(destination, picture, nil)
        message = CGImageDestinationFinalize(destination)
            ? "Saved \(url.lastPathComponent) (\(picture.width) × \(picture.height))."
            : "Could not write \(url.lastPathComponent)."
    }

    // MARK: Long press, configurations, marker cards

    @Published var markerCard: MarkerCardInfo?

    /// The view's long press at `point` (view points): a marker's card, a joint, or the core's note.
    func hold(at point: CGPoint) {
        guard let session, !isPicture else { return }
        let (width, height) = frameSize()
        let x = Float(point.x / max(1, viewSize.width)) * Float(width)
        let y = Float(point.y / max(1, viewSize.height)) * Float(height)
        let outcome = session.hold(width: width, height: height, camera: camera, flags: settings.renderFlags,
                                   x: x, y: y)
        handle(outcome)
    }

    func loadStageConfig(url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
            message = "Could not read \(url.lastPathComponent)."
            return
        }
        let slot = session.map { dmc_stage_slot($0.handle) } ?? 0
        handle(ReaderCore.loadStageConfig(name: url.lastPathComponent, bytes: data, slot: Int(slot)))
        refreshAfterChange()
    }

    /// A cutscene's .ogg the user picked.
    func loadCutsceneSound(url: URL) {
        guard let session else { return }
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
            message = "Could not read \(url.lastPathComponent)."
            return
        }
        var result = data.withUnsafeBytes { raw in
            dmc_cutscene_sound_load(session.handle, url.lastPathComponent, raw.bindMemory(to: UInt8.self).baseAddress, raw.count)
        }
        defer { dmc_outcome_free(&result) }
        message = String(cString: result.message)
        if playing { startMotion() }  // the new sound joins the playing cutscene
    }

    func runCard(button: MenuRow) {
        guard let card = markerCard else { return }
        markerCard = nil
        handle(ReaderCore.runCardAction(marker: card.marker, id: button.id))
    }

    /// The one platform step the core leaves to the shell.
    private func handle(_ outcome: Outcome) {
        if !outcome.message.isEmpty { message = outcome.message }
        if let opened = outcome.opened, let session {
            stack.append(session)
            show(opened)
            return
        }
        switch outcome.step {
        case Int32(DMC_STEP_SHOW_MARKER_CARD.rawValue):
            markerCard = ReaderCore.markerCard(Int(outcome.marker))
        case Int32(DMC_STEP_RESET_VIEW.rawValue):
            resetCamera()
        case Int32(DMC_STEP_PICK_STAGE_CONFIG.rawValue):
            OpenPanel.choose { [weak self] in self?.loadStageConfig(url: $0) }
        case Int32(DMC_STEP_PICK_CUTSCENE_SOUND.rawValue):
            OpenPanel.choose { [weak self] in self?.loadCutsceneSound(url: $0) }
        default:
            requestFrame()
        }
    }

    // MARK: Photo camera and fly mode

    /// The photo camera's picture saved as PNG.
    func savePhoto(to url: URL) {
        guard let session else { return }
        let (width, height) = frameSize()
        guard let photo = session.capturePhoto(viewWidth: width, viewHeight: height, camera: camera,
                                               flags: settings.renderFlags, motionFrame: motionFrame),
              let picture = Self.cgImage(photo),
              let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else {
            message = "The camera took no picture of this view."
            return
        }
        CGImageDestinationAddImage(destination, picture, nil)
        message = CGImageDestinationFinalize(destination)
            ? "Saved \(url.lastPathComponent) (\(picture.width) × \(picture.height))." : "Could not write \(url.lastPathComponent)."
    }

    /// The photo camera's RAW picture saved as DNG.
    func saveDNG(to url: URL) {
        guard let session else { return }
        FileManager.default.createFile(atPath: url.path, contents: nil)
        guard let handle = try? FileHandle(forWritingTo: url) else {
            message = "Could not write \(url.lastPathComponent)."
            return
        }
        defer { try? handle.close() }
        let (width, height) = frameSize()
        message = session.captureDNG(viewWidth: width, viewHeight: height, camera: camera, flags: settings.renderFlags,
                                     motionFrame: motionFrame, fd: handle.fileDescriptor)
            ? "Saved \(url.lastPathComponent)." : "The camera took no RAW picture of this view."
    }

    @Published private(set) var flying = false
    private var flyTimer: Timer?
    private var flyKeys = Set<String>()
    private var flyLast = Date()

    func toggleFly() {
        guard let session else { return }
        if flying {
            flyTimer?.invalidate()
            flyTimer = nil
            session.flyLeave(yaw: camera.yaw, pitch: camera.pitch)
            flying = false
            requestFrame()
            return
        }
        let (width, height) = frameSize()
        guard session.flyEnter(width: width, height: height, camera: camera, flags: settings.renderFlags) else {
            message = "This view has no eye to fly from."
            return
        }
        flying = true
        flyLast = Date()
        message = "Fly: W A S D move, arrow keys look, F leaves."
        flyTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.flyStep() }
        }
    }

    /// Keys held while flying (a lowercase letter, or "up"/"down"/"left"/"right").
    func flyKey(_ key: String, down: Bool) {
        if down { flyKeys.insert(key) } else { flyKeys.remove(key) }
    }

    /// The keys as the two on-screen sticks Android has: pixels from the stick's base.
    private func flyStep() {
        guard flying, let session else { return }
        let now = Date()
        let dt = Float(min(0.1, now.timeIntervalSince(flyLast)))
        flyLast = now
        let reach: Float = 120
        func stick(_ left: String, _ right: String, _ up: String, _ down: String) -> (Float, Float)? {
            let x: Float = (flyKeys.contains(right) ? reach : 0) - (flyKeys.contains(left) ? reach : 0)
            let y: Float = (flyKeys.contains(down) ? reach : 0) - (flyKeys.contains(up) ? reach : 0)
            return x == 0 && y == 0 ? nil : (x, y)
        }
        let step = session.flyTick(yaw: camera.yaw, pitch: camera.pitch, move: stick("a", "d", "w", "s"),
                                   look: stick("left", "right", "up", "down"), dt: dt)
        if step.yaw != camera.yaw || step.pitch != camera.pitch || !flyKeys.isEmpty {
            camera.yaw = step.yaw
            camera.pitch = step.pitch
        }
    }

    private func refreshAfterChange() {
        guard let session else { return }
        let menu = session.companionRows()
        actions = menu.rows
        moreActions = menu.more
        effects = session.effectRows()
        requestFrame()
    }

    // MARK: Actions

    /// A companion menu row chosen: the core runs it and says what is left to do.
    func run(action: MenuRow) {
        guard let session else { return }
        let outcome = session.runCompanionAction(action.id)
        if outcome.opened != nil { handle(outcome); return }
        handle(outcome)
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
