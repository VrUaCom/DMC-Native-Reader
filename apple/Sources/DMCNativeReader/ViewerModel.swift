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
    @Published var message: String?
    @Published var camera = ViewCamera() { didSet { requestFrame() } }

    /// Sessions under the current one (Back returns to the last).
    var stack: [ReaderSession] = []
    /// The settings schema and the user's stored choices (UserDefaults), applied by the core.
    let settingRows = ReaderCore.settingsSchema()
    @Published private(set) var stored: [String: Float] =
        (UserDefaults.standard.dictionary(forKey: "settings") as? [String: Float]) ?? [:]
    lazy var settings = ReaderCore.apply(stored: stored)

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
    var viewSize = CGSize(width: 800, height: 600)
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
    @Published var motionCards: [MotionCard] = []
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

    /// The file being opened (read and parsed off the main thread), if any.
    @Published private(set) var opening: String?
    private var openRequest = 0

    func open(url: URL) {
        let name = url.lastPathComponent
        openRequest += 1
        let request = openRequest
        opening = name
        // The file the reader closed on the last time opens as its description.
        let describeOnly = CrashReports.takeClosedOn(name)
        CrashReports.opening(name)
        // The core parses without the session lock (B7): a large PAC opens
        // while the window stays live and the current view keeps drawing.
        Task { @MainActor [weak self] in
            let result = await Task.detached(priority: .userInitiated) { () -> Result<(ReaderSession, Data), OpenFailure> in
                let access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                do {
                    let data = try Data(contentsOf: url, options: .mappedIfSafe)
                    return ReaderSession.open(name: name, bytes: data, describeOnly: describeOnly).map { ($0, data) }
                } catch {
                    return .failure(OpenFailure(message: "Could not read \(name): \(error.localizedDescription)"))
                }
            }.value
            CrashReports.shown(name)
            guard let self, request == self.openRequest else { return }  // a newer file was picked
            self.opening = nil
            switch result {
            case .success(let (opened, data)):
                self.stack.removeAll()
                self.sourceFile = (name, data)
                self.addedPacs.removeAll()
                self.variantIndex = 0
                self.variants = opened.openedAs == .assembledPac ? ReaderSession.variantNames(name) : []
                self.staged.removeAll()
                self.stagedMotions.removeAll()
                self.show(opened)
                // An opened stage brings its own effect bank and configuration from beside it.
                self.findCommonEffects(near: url)
                if opened.openedAs == .stage {
                    self.findStageArchives(near: url, stage: name, slot: 1, effects: true, config: true)
                }
            case .failure(let failure):
                self.message = failure.message
            }
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

    // MARK: UV maps, the Info picture

    var canShowUV: Bool { !isPicture && (session?.state.contains(.canShowUv) ?? false) }
    @Published private(set) var dualPreview = false
    @Published private(set) var infoPreviewShown = false

    func openUVMaps() {
        guard let session else { return }
        guard let gallery = session.openUVGallery() else {
            message = "UV maps unavailable: incomplete bindings"
            return
        }
        stack.append(session)
        show(gallery)
    }

    func setInfoPreview(_ info: Bool) {
        guard let session, session.hasDualPreview, session.setInfoPreview(info) else { return }
        infoPreviewShown = session.infoPreviewActive
        if let picture = session.picture() { image = Self.cgImage(picture) }
    }

    func goBack() {
        guard let previous = stack.popLast() else { return }
        show(previous)
    }

    func show(_ next: ReaderSession) {
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
        motionCards = isPicture ? [] : next.motionCards(staged: stagedMotionNames)
        let menu = next.companionRows()
        actions = menu.rows
        moreActions = menu.more
        effects = isPicture ? [] : next.effectRows()
        soundPlayer.stop()
        sounds = next.soundRows().rows
        hitboxAttack = nil
        classEvents = isPicture ? [] : next.classEvents()
        dualPreview = next.hasDualPreview
        infoPreviewShown = dualPreview && next.infoPreviewActive
        stageCollisionShown = false
        attackIDs = next.state.contains(.canShowHitboxes) ? next.attackIDs() : []
        camera = startCamera(of: next)  // draws the first frame of a 3D session
    }

    // MARK: Info

    func infoText() -> String {
        guard let session else { return welcome }
        return session.infoText(navigationDepth: stack.count, backTitle: stack.last?.title ?? "", staged: staged)
    }

    func healthReport() -> String { session?.health().report ?? "" }

    // MARK: Frames

    func viewChanged(size: CGSize, scale: CGFloat) {
        guard size != viewSize || scale != self.scale else { return }
        viewSize = size
        self.scale = scale
        requestFrame()
    }

    /// Zooms the view by `factor` (within the core's limits).
    func zoom(by factor: Float) {
        guard session != nil, !isPicture else { return }
        camera.zoom = dmc_pinch_zoom(camera.zoom, factor)
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
        guard let session else { return }
        // A staged motion file: the shell reads it, the core loads it.
        if card.libraryIndex < 0, card.stagedIndex >= 0, card.stagedIndex < stagedMotions.count {
            loadMotion(url: stagedMotions[card.stagedIndex])
            activeCard = card.id
            return
        }
        guard card.libraryIndex >= 0 else { return }
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
        motionCards = session.motionCards(staged: stagedMotionNames)
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
    /// The base room's effect bank and configuration files (slot 0).
    var baseEffectsName: String?
    var baseConfigName: String?
    /// The common effect bank (static_effect.pac) and the folder it came from.
    @Published var commonEffectsName: String?
    var commonEffectsFolder: URL?
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
            afterNewRoom(url: url)
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

    // MARK: Shell steps of the companion menu (ShellActions.swift)

    /// The file opened from disk: the source for adding PACs or MOD parts.
    var sourceFile: (name: String, data: Data)?
    /// PACs added to the opened character (weapons, motions), kept for re-assembly.
    var addedPacs: [(name: String, data: Data)] = []
    /// The opened archive's in-game looks (dmc_variant_names) and the one shown.
    @Published var variants: [String] = []
    @Published var variantIndex = 0
    @Published var askTextureFormat = false
    @Published var partsShown: [PartInfo] = []
    @Published var diagnosticsText = ""
    @Published var diagnosticsTitle = "Diagnostics"
    @Published var classEvents: [String] = []
    @Published var roomBroken = false
    /// Files staged beside the model, listed in Info.
    var staged: [(role: String, name: String)] = []
    /// Motion files staged beside the model: cards of the animation strip.
    var stagedMotions: [URL] = []
    var stagedMotionNames: [String] { stagedMotions.map(\.lastPathComponent) }

    // MARK: Crash report

    /// The last run's crash report, shown once at start.
    @Published var crashReport: String?

    // MARK: Collision

    /// Hitboxes: nil off, -1 every attack, else the attack shown.
    @Published private(set) var hitboxAttack: Int?
    @Published private(set) var stageCollisionShown = false
    @Published private(set) var attackIDs: [Int] = []
    @Published var collisionReport: CollisionReport?
    var canShowHitboxes: Bool { session?.state.contains(.canShowHitboxes) ?? false }
    var canShowStageCollision: Bool {
        (session?.stageCollisionSources ?? 0) > 0 || (roomName != nil && dmc_room_collision_source_count() > 0)
    }

    /// Shows `attack`'s hitboxes (-1: all), or hides them (nil).
    func showHitboxes(_ attack: Int?) {
        guard let session else { return }
        hitboxAttack = attack
        if let attack {
            let label = session.selectAttack(attack)
            message = "Hitboxes: \(label)"
        } else {
            message = "Hitboxes off"
        }
        requestFrame()
    }

    /// Off, every attack, then each attack in turn, as the Android button.
    func cycleHitboxes() {
        guard canShowHitboxes else { return }
        guard let current = hitboxAttack else { return showHitboxes(-1) }
        let next = current == -1 ? 0 : (attackIDs.firstIndex(of: current).map { $0 + 1 } ?? attackIDs.count)
        showHitboxes(next < attackIDs.count ? attackIDs[next] : nil)
    }

    func toggleStageCollision() {
        guard canShowStageCollision else { return }
        stageCollisionShown.toggle()
        message = stageCollisionShown ? "Stage collision shown" : "Stage collision hidden (still active)"
        requestFrame()
    }

    func showCollisionReport(attack: Bool) {
        collisionReport = session?.collisionReport(attack: attack)
        if collisionReport == nil { message = "No collision to report." }
    }

    // MARK: Sounds

    @Published private(set) var sounds: [MenuRow] = []
    private let soundPlayer = SoundPlayer()

    func playSound(_ row: MenuRow) {
        guard let session else { return }
        let pcm = session.soundPCM(row.id)
        if pcm.samples.isEmpty {
            message = pcm.message.isEmpty ? "This sound could not be decoded." : pcm.message
            return
        }
        soundPlayer.play(samples: pcm.samples, rate: pcm.rate)
        if !pcm.message.isEmpty { message = pcm.message }
    }

    func stopSound() { soundPlayer.stop() }

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
        if slot == 0 { baseConfigName = url.lastPathComponent }
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
    func handle(_ outcome: Outcome) {
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

    // MARK: Camera settings and viewfinder

    let cameraRows = ReaderCore.cameraSettings()
    @Published var cameraChoices: [Int] = []
    @Published private(set) var viewfinder: Viewfinder?
    @Published private(set) var dials: [(key: Int, title: String, value: String)] = []
    /// The frame size the viewfinder was laid out for (pixels).
    private(set) var finderFrameSize = CGSize(width: 1, height: 1)

    func loadCameraChoices() {
        let stored = UserDefaults.standard.dictionary(forKey: "camera") as? [String: Int] ?? [:]
        cameraChoices = cameraRows.map { stored[$0.id] ?? $0.start }
        ReaderCore.setCameraChoices(cameraChoices)
        refreshFinder()
    }

    func setCamera(_ row: CameraSettingRow, option: Int) {
        guard let index = cameraRows.firstIndex(of: row) else { return }
        if cameraChoices.count != cameraRows.count { cameraChoices = cameraRows.map(\.start) }
        cameraChoices[index] = option
        var stored = UserDefaults.standard.dictionary(forKey: "camera") as? [String: Int] ?? [:]
        stored[row.id] = option
        // What changes with it (a camera's M dial sets the shutter, ...): the core's follow-up.
        var follow = dmc_camera_follow_up()
        if dmc_camera_follow_up_of(Int32(row.key), Int32(option), &follow) {
            for i in 0..<follow.count {
                let key = Int(follow.keys[i])
                if let other = cameraRows.firstIndex(where: { $0.key == key }) {
                    cameraChoices[other] = Int(follow.options[i])
                    stored[cameraRows[other].id] = Int(follow.options[i])
                }
            }
            if let text = follow.message.map({ String(cString: $0) }), !text.isEmpty { message = text }
            dmc_camera_follow_up_free(&follow)
        }
        UserDefaults.standard.set(stored, forKey: "camera")
        ReaderCore.setCameraChoices(cameraChoices)
        refreshFinder()
        requestFrame()
    }

    /// The viewfinder and the dials, when the camera mode is on.
    func refreshFinder() {
        guard let session, !isPicture, dmc_camera_mode() else {
            if viewfinder != nil { viewfinder = nil }
            if !dials.isEmpty { dials = [] }
            return
        }
        let (width, height) = frameSize()
        finderFrameSize = CGSize(width: width, height: height)
        viewfinder = session.viewfinder(width: width, height: height, zoom: camera.zoom, recording: recording, seconds: 0)
        dials = ReaderCore.cameraDials()
    }

    private let recorder = ViewRecorder()
    @Published private(set) var recording = false

    /// Starts or stops a video of the view (the core's frames, AVAssetWriter's file).
    func toggleRecording(to url: URL? = nil) {
        if recording {
            recorder.stop { [weak self] text in self?.message = text }
            recording = false
            return
        }
        guard let session, let url else { return }
        let (width, height) = frameSize()
        let error = recorder.start(url: url, viewWidth: width, viewHeight: height) { [weak self] rgba, capacity in
            guard let self else { return false }
            return session.videoFrame(viewWidth: width, viewHeight: height, camera: self.camera,
                                      flags: self.settings.renderFlags, motionFrame: self.motionFrame,
                                      rgba: rgba, capacity: capacity)
        }
        if let error { message = error } else { recording = true; message = "Recording… (Camera menu → Stop recording)" }
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

    func refreshAfterChange() {
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
        motionCards = session.motionCards(staged: stagedMotionNames)
        requestFrame()
    }

    func requestFrame() {
        guard let session, !isPicture else { return }
        if dmc_camera_mode() || viewfinder != nil { refreshFinder() }
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
        if hitboxAttack != nil { flags |= RenderFlag.collision }
        if stageCollisionShown { flags |= RenderFlag.roomCollision }
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
    func frameSize() -> (Int, Int) {
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
