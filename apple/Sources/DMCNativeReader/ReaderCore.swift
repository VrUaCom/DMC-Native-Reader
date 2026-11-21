// The reader's C API (dmc_reader.h) in Swift terms. Every decision and every
// line of text comes from the C++ core; this file only converts types and
// owns the handles. No Apple framework is used here, so it also compiles on
// Linux, where its C interop is checked.
import CDMCReader
import Foundation
import QuartzCore

/// How a file opened, decided by the core (spider open_resource).
enum OpenedAs: Int {
    case rejected = 0
    case plain = 1
    case assembledPac = 2
    case stage = 3
}

/// RGBA8 pixels, rows top down, `width * 4` bytes each.
struct RGBAImage {
    let width: Int
    let height: Int
    let pixels: [UInt8]
}

/// The camera of one frame. Its numbers change only through the core's touch
/// math (#45: `dmc_view_*`), so a drag, a pinch, a twist or a flick moves a
/// model here exactly as it does on Android.
struct ViewCamera: Equatable {
    var yaw: Float
    var pitch: Float
    var zoom: Float
    var panX: Float = 0
    var panY: Float = 0
    var roomYaw: Float = 0

    /// The core's start view: a front three-quarter view.
    init() {
        var view = dmc_view()
        dmc_view_reset(&view)
        yaw = view.yaw
        pitch = view.pitch
        zoom = view.zoom
    }

    private var view: dmc_view {
        var v = dmc_view()
        v.yaw = yaw; v.pitch = pitch; v.zoom = zoom
        v.pan_x = panX; v.pan_y = panY; v.room_yaw = roomYaw
        return v
    }

    private mutating func take(_ v: dmc_view) {
        yaw = v.yaw; pitch = v.pitch; zoom = v.zoom
        panX = v.pan_x; panY = v.pan_y; roomYaw = v.room_yaw
    }

    /// The pointer moved by dx, dy points: grab-and-turn.
    @discardableResult mutating func drag(dx: Float, dy: Float) -> Bool {
        var v = view
        guard dmc_view_drag(&v, dx, dy) else { return false }
        take(v)
        return true
    }

    /// Two fingers on the trackpad moved together over a width x height view.
    @discardableResult mutating func pan(dx: Float, dy: Float, width: Int, height: Int) -> Bool {
        var v = view
        guard dmc_view_pan(&v, dx, dy, Int32(width), Int32(height)) else { return false }
        take(v)
        return true
    }

    /// Two fingers turned by `radians`.
    @discardableResult mutating func twist(radians: Float, roomShown: Bool) -> Bool {
        var v = view
        guard dmc_view_twist(&v, radians, roomShown) else { return false }
        take(v)
        return true
    }

    /// A pinch by `scale` since the gesture began at `startZoom`.
    mutating func pinch(startZoom: Float, scale: Float) { zoom = dmc_pinch_zoom(startZoom, scale) }

    /// One step of the inertia after a flick; false once it has stopped.
    mutating func spin(_ spin: inout dmc_spin, dt: Float) -> Bool {
        var v = view
        let going = dmc_spin_step(&v, &spin, dt)
        take(v)
        return going
    }
}

/// Black Widow state bits (spider/black_widow.h) the shell reads.
struct SessionState: OptionSet {
    let rawValue: UInt64
    static let uvMapView = SessionState(rawValue: 1 << 18)
    static let canShowUv = SessionState(rawValue: 1 << 14)
    static let canShowHitboxes = SessionState(rawValue: 1 << 33)
    static let canShowStageCollision = SessionState(rawValue: 1 << 34)
    static let canBreakRoom = SessionState(rawValue: 1 << 36)
}

/// What the stored settings mean for the view (dmc_apply_settings).
struct AppliedSettings {
    var renderFlags: UInt32
    var maxSide: Int
    var shadows: Bool
}

/// One open resource of the core. Its stored properties are immutable and every
/// call goes through the C API, which takes the core's session lock for the whole
/// call (`session_lock.h`, checked by `c_api_threads_test`); a frame on the render
/// queue and a change from the main thread therefore never overlap. That contract
/// is what makes it safe to hand the session to the render queue.
final class ReaderSession: @unchecked Sendable {
    let handle: OpaquePointer
    let title: String
    let openedAs: OpenedAs
    let fileName: String

    private init(handle: OpaquePointer, title: String, openedAs: OpenedAs, fileName: String) {
        self.handle = handle
        self.title = title
        self.openedAs = openedAs
        self.fileName = fileName
    }

    /// A session the core made (an action's opened file); this object closes it.
    convenience init(adopting handle: OpaquePointer, title: String) {
        self.init(handle: handle, title: title, openedAs: .plain, fileName: title)
    }

    deinit { dmc_close(handle) }

    /// Opens `bytes` named `name`; on rejection the core's reason comes back as text.
    static func open(name: String, bytes: Data, describeOnly: Bool = false) -> Result<ReaderSession, OpenFailure> {
        var result = bytes.withUnsafeBytes { raw -> dmc_open_result in
            let base = raw.bindMemory(to: UInt8.self).baseAddress
            return describeOnly ? dmc_open_description(name, base, raw.count) : dmc_open(name, base, raw.count)
        }
        let title = takeString(result.title) ?? name
        result.title = nil
        guard let handle = result.session else {
            return .failure(OpenFailure(message: title))
        }
        let opened = OpenedAs(rawValue: Int(result.opened_as.rawValue)) ?? .plain
        return .success(ReaderSession(handle: handle, title: title, openedAs: opened, fileName: name))
    }

    /// Child `index` of an archive (a PAC's file), opened as its own session.
    func openChild(_ index: Int) -> ReaderSession? {
        guard let child = dmc_open_child(handle, index) else { return nil }
        let title = takeString(dmc_child_title(handle, index)) ?? "\(fileName) #\(index)"
        return ReaderSession(handle: child, title: title, openedAs: .plain, fileName: title)
    }

    /// The model's UV maps as a session of their own (one child per map).
    func openUVGallery() -> ReaderSession? {
        guard let gallery = dmc_open_uv_gallery(handle) else { return nil }
        return ReaderSession(handle: gallery, title: "\(title) · UV", openedAs: .plain, fileName: fileName)
    }

    /// A texture with an Info picture too: which of the two is shown.
    var hasDualPreview: Bool { dmc_has_dual_preview(handle) }
    var infoPreviewActive: Bool { dmc_info_preview_active(handle) }
    @discardableResult func setInfoPreview(_ info: Bool) -> Bool { dmc_set_info_preview(handle, info) }

    var childCount: Int { Int(dmc_child_count(handle)) }

    func childTitle(_ index: Int) -> String {
        takeString(dmc_child_title(handle, index)) ?? "#\(index)"
    }

    var state: SessionState { SessionState(rawValue: dmc_state_flags(handle)) }

    /// Info, written by the core: structure, evidence, navigation, staged files.
    func infoText(navigationDepth: Int, backTitle: String, staged: [(role: String, name: String)] = []) -> String {
        // The staged files' strings live until the call returns.
        let roles = staged.map { strdup($0.role) }
        let names = staged.map { strdup($0.name) }
        defer { (roles + names).forEach { free($0) } }
        let files = zip(roles, names).map { dmc_staged_file(role: $0, name: $1) }
        return files.withUnsafeBufferPointer { buffer in
            takeString(dmc_info_text(handle, title, navigationDepth, backTitle, buffer.baseAddress, buffer.count))
        } ?? title
    }

    /// File health: 0 fine, 1 notice (orange), 2 problem (red), and its report.
    func health() -> (level: Int, report: String) {
        var report: UnsafeMutablePointer<CChar>?
        let level = dmc_file_health(handle, fileName, &report)
        return (Int(level), takeString(report) ?? "")
    }

    /// The session's own picture (a texture, a description), if it shows one.
    func picture() -> RGBAImage? {
        var width: Int32 = 0
        var height: Int32 = 0
        guard dmc_image_preview_size(handle, &width, &height), width > 0, height > 0 else { return nil }
        var pixels = [UInt8](repeating: 0, count: Int(width) * Int(height) * 4)
        let ok = pixels.withUnsafeMutableBufferPointer { buffer in
            dmc_image_preview(handle, buffer.baseAddress, buffer.count)
        }
        return ok ? RGBAImage(width: Int(width), height: Int(height), pixels: pixels) : nil
    }

    /// A frame of the 3D view: its pixels, or shown straight on the attached
    /// window (dmc_gpu_attach); nil when the session draws none.
    enum Frame {
        case pixels(RGBAImage)
        case shown
    }

    func render(width: Int, height: Int, camera: ViewCamera, flags: UInt32,
                motionFrame: Float = .nan, window: CAMetalLayer? = nil) -> Frame? {
        guard width > 0, height > 0 else { return nil }
        var view = dmc_view()
        view.yaw = camera.yaw
        view.pitch = camera.pitch
        view.zoom = camera.zoom
        view.render_flags = flags
        view.pan_x = camera.panX
        view.pan_y = camera.panY
        view.room_yaw = camera.roomYaw
        view.motion_frame = motionFrame
        view.window = window.map { Unmanaged.passUnretained($0).toOpaque() }
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let status = pixels.withUnsafeMutableBufferPointer { buffer in
            dmc_render(handle, Int32(width), Int32(height), &view, buffer.baseAddress, buffer.count)
        }
        if status == 0 { return nil }
        if status & Int32(DMC_RENDER_SHOWN) != 0 { return .shown }
        return .pixels(RGBAImage(width: width, height: height, pixels: pixels))
    }
}

extension RGBAImage {
    /// A copy of a picture the core made.
    init(copying image: dmc_image) {
        let count = Int(image.width) * Int(image.height) * 4
        self.init(width: Int(image.width), height: Int(image.height),
                  pixels: Array(UnsafeBufferPointer(start: image.rgba, count: count)))
    }
}

/// One card of the animation strip (spider/motion_strip): a motion of the
/// session's library or a staged file, with the MotionScripts that drive it.
struct MotionCard: Identifiable, Hashable {
    struct Script: Hashable {
        let scriptIndex: Int
        let slot: Int
        let label: String
        let description: String
    }
    let id: Int
    let name: String
    let libraryIndex: Int
    let stagedIndex: Int
    let kind: String
    let stem: String
    let description: String
    let separatorBefore: Bool
    let scripts: [Script]
}

/// A row of a menu the core wrote: a stable id the shell sends back.
struct MenuRow: Identifiable, Hashable {
    let id: Int
    let label: String
    let icon: String
}

/// One photo camera setting of the core's schema.
struct CameraSettingRow: Identifiable, Hashable {
    let key: Int
    let id: String
    let section: String
    let label: String
    let hint: String
    let options: [String]
    let start: Int
}

/// The viewfinder over the view: frame and guide lines in view pixels, the status line.
struct Viewfinder: Equatable {
    let frame: CGRect
    let lines: [CGRect]          // (x0, y0) as origin, (x1 - x0, y1 - y0) as size
    let status: String
}

/// A cutscene's subtitle: text and its layout in fractions of the view.
struct Subtitle: Equatable {
    let text: String
    let centreX: Float
    let top: Float
    let size: Float
    let alpha: Float
    let front: (UInt8, UInt8, UInt8)
    let back: (UInt8, UInt8, UInt8)
    static func == (a: Subtitle, b: Subtitle) -> Bool {
        a.text == b.text && a.centreX == b.centreX && a.top == b.top && a.size == b.size && a.alpha == b.alpha
    }
}

/// A configuration marker's card, as the core wrote it.
struct MarkerCardInfo: Identifiable {
    struct Row: Hashable { let name: String; let value: String }
    let marker: Int
    let title: String
    let rows: [Row]
    let buttons: [MenuRow]
    var id: Int { marker }
}

/// What the core tells the shell after an action.
struct Outcome {
    let ok: Bool
    let step: Int32
    let message: String
    let marker: Int32
    let title: String
    var opened: ReaderSession?
}

extension ReaderSession {
    /// The animation strip: the library's motions and the scripts that drive them.
    func motionCards(staged: [String] = []) -> [MotionCard] {
        let names = staged.map { strdup($0) }
        defer { names.forEach { free($0) } }
        let built = names.map { UnsafePointer($0) }.withUnsafeBufferPointer { buffer in
            dmc_motion_strip_build(handle, true, buffer.baseAddress, buffer.count)
        }
        guard let strip = built else { return [] }
        defer { dmc_motion_strip_free(strip) }
        var cards: [MotionCard] = []
        for index in 0..<dmc_motion_strip_count(strip) {
            guard let card = dmc_motion_strip_card(strip, index)?.pointee else { continue }
            var scripts: [MotionCard.Script] = []
            for s in 0..<card.script_count {
                let button = card.scripts[s]
                scripts.append(.init(scriptIndex: Int(button.script_index), slot: Int(button.slot),
                                     label: String(cString: button.label),
                                     description: String(cString: button.description)))
            }
            cards.append(MotionCard(id: index, name: String(cString: card.name),
                                    libraryIndex: Int(card.library_index), stagedIndex: Int(card.staged_index),
                                    kind: String(cString: card.kind), stem: String(cString: card.stem),
                                    description: String(cString: card.description),
                                    separatorBefore: card.separator_before, scripts: scripts))
        }
        return cards
    }

    func loadLibraryMotion(_ index: Int) -> String {
        takeString(dmc_load_library_motion(handle, index)) ?? ""
    }

    func loadScriptedMotion(script: Int, motion: Int) -> String {
        takeString(dmc_load_scripted_motion(handle, script, motion)) ?? ""
    }

    func loadMotion(name: String, bytes: Data) -> String {
        bytes.withUnsafeBytes { raw in
            takeString(dmc_load_motion(handle, name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count)) ?? ""
        }
    }

    /// The loaded motion's last frame and loop start; nil without a motion.
    func motionRange() -> (end: Float, loopStart: Float)? {
        var end: Float = 0
        var loop: Float = 0
        return dmc_motion_range(handle, &end, &loop) ? (end, loop) : nil
    }

    /// A stage's start view (the game's camera at its first spawn point).
    func stageStartView() -> (yaw: Float, pitch: Float, zoom: Float)? {
        var out: [Float] = [0, 0, 0]
        let ok = out.withUnsafeMutableBufferPointer { dmc_stage_start_view(handle, $0.baseAddress) }
        return ok ? (out[0], out[1], out[2]) : nil
    }

    /// The companion menu's rows and its folded "Other actions".
    func companionRows() -> (rows: [MenuRow], more: [MenuRow]) {
        var facts = dmc_shell_facts()
        facts.root_scene = true
        facts.room_shown = true
        guard let menu = dmc_companion_menu(handle, &facts) else { return ([], []) }
        defer { dmc_companion_free(menu) }
        return (rows(of: dmc_companion_get(menu, Int32(DMC_COMPANION_ROWS.rawValue))),
                rows(of: dmc_companion_get(menu, Int32(DMC_COMPANION_MORE.rawValue))))
    }

    func runCompanionAction(_ id: Int) -> Outcome {
        var opened: OpaquePointer?
        var result = dmc_run_companion_action(handle, Int32(id), &opened)
        defer { dmc_outcome_free(&result) }
        var outcome = Outcome(ok: result.ok, step: result.step, message: String(cString: result.message),
                              marker: result.marker, title: String(cString: result.title), opened: nil)
        if let opened {
            let title = outcome.title.isEmpty ? self.title : outcome.title
            outcome.opened = ReaderSession(adopting: opened, title: title)
        }
        return outcome
    }

    /// The effects the view can play, and playing one.
    func effectRows() -> [MenuRow] {
        guard let menu = dmc_effect_menu(handle) else { return [] }
        defer { dmc_menu_free(menu) }
        return rows(of: menu)
    }

    /// Attack ids with hitboxes on the model's bones.
    func attackIDs() -> [Int] {
        let count = dmc_collision_attack_ids(handle, nil, 0)
        guard count > 0 else { return [] }
        var ids = [Int32](repeating: 0, count: count)
        let written = ids.withUnsafeMutableBufferPointer { dmc_collision_attack_ids(handle, $0.baseAddress, count) }
        return ids.prefix(min(written, count)).map(Int.init)
    }

    /// Shows only `attack`'s hitboxes (-1: all); the core's description.
    func selectAttack(_ attack: Int) -> String { takeString(dmc_select_collision_attack(handle, Int32(attack))) ?? "" }

    var stageCollisionSources: Int { Int(dmc_stage_collision_source_count(handle)) }

    /// The hitbox report (`attack`) or the stage's HITS report.
    func collisionReport(attack: Bool) -> CollisionReport? {
        guard let report = dmc_collision_report(handle, attack) else { return nil }
        defer { dmc_report_free(report) }
        let groups = (0..<dmc_report_group_count(report)).compactMap { index -> CollisionReport.Group? in
            guard let g = dmc_report_group_at(report, index)?.pointee else { return nil }
            return CollisionReport.Group(id: index, rgb: g.rgb, heading: g.heading.map { String(cString: $0) } ?? "",
                                         lines: g.lines.map { String(cString: $0) } ?? "")
        }
        return CollisionReport(title: dmc_report_title(report).map { String(cString: $0) } ?? "",
                               summary: dmc_report_summary(report).map { String(cString: $0) } ?? "",
                               groups: groups)
    }

    /// The file's sounds; `message` is the core's line when there are none.
    func soundRows() -> (rows: [MenuRow], message: String) {
        guard let menu = dmc_sound_menu(handle) else { return ([], "") }
        defer { dmc_menu_free(menu) }
        let message = dmc_menu_message(menu).map { String(cString: $0) } ?? ""
        return (rows(of: menu), message)
    }

    /// Sound `index` as 16-bit mono samples and its rate, or the core's reason.
    func soundPCM(_ index: Int) -> (samples: [Int16], rate: UInt32, message: String) {
        var pcm = dmc_sound_pcm(handle, Int32(index))
        defer { dmc_pcm_free(&pcm) }
        let message = pcm.message.map { String(cString: $0) } ?? ""
        guard let samples = pcm.samples, pcm.sample_count > 0 else { return ([], 0, message) }
        return (Array(UnsafeBufferPointer(start: samples, count: pcm.sample_count)), pcm.sample_rate, message)
    }

    func playEffect(_ id: Int) -> String {
        var result = dmc_play_effect(handle, Int32(id))
        defer { dmc_outcome_free(&result) }
        return String(cString: result.message)
    }

    /// The stage's PNG export picture (nil: the session has none).
    func exportPicture() -> RGBAImage? {
        guard let image = dmc_render_export(handle) else { return nil }
        defer { dmc_image_free(image) }
        return RGBAImage(copying: image.pointee)
    }

    /// The view's long press at (x, y) of a width x height frame.
    func hold(width: Int, height: Int, camera: ViewCamera, flags: UInt32, x: Float, y: Float) -> Outcome {
        var view = dmc_view()
        view.yaw = camera.yaw; view.pitch = camera.pitch; view.zoom = camera.zoom
        view.render_flags = flags; view.pan_x = camera.panX; view.pan_y = camera.panY
        view.room_yaw = camera.roomYaw; view.motion_frame = .nan
        var result = dmc_hold(handle, Int32(width), Int32(height), &view, x, y, true, false)
        defer { dmc_outcome_free(&result) }
        return Outcome(ok: result.ok, step: result.step, message: String(cString: result.message),
                       marker: result.marker, title: String(cString: result.title), opened: nil)
    }

    /// A cutscene's mixed sound: rate and channels (nil: none).
    func cutsceneAudioFormat() -> (rate: Int, channels: Int)? {
        var rate: UInt32 = 0
        var channels: UInt32 = 0
        return dmc_cutscene_audio_format(handle, &rate, &channels) && rate > 0 && channels > 0
            ? (Int(rate), Int(channels)) : nil
    }

    /// The sample frame heard at motion frame `frame`.
    func cutsceneAudioPosition(frame: Float) -> Int64 { dmc_cutscene_audio_position(handle, frame) }

    /// Reads up to `frames` sample frames from `position` (interleaved int16); returns values written.
    func readCutsceneAudio(position: Int64, frames: Int, into buffer: UnsafeMutablePointer<Int16>, capacity: Int) -> Int {
        Int(dmc_cutscene_audio_read(handle, position, frames, buffer, capacity))
    }

    /// The subtitle shown now (text and how the core lays it out), nil when none.
    func subtitle() -> Subtitle? {
        var s = dmc_subtitle()
        guard dmc_cutscene_subtitle(handle, &s) else { return nil }
        defer { dmc_subtitle_free(&s) }
        return Subtitle(text: s.text.map { String(cString: $0) } ?? "", centreX: s.centre_x, top: s.top,
                        size: s.size, alpha: s.alpha, front: (s.front.0, s.front.1, s.front.2),
                        back: (s.back.0, s.back.1, s.back.2))
    }

    /// The dmc_view of a camera, a frame's flags and the motion frame.
    func view(of camera: ViewCamera, flags: UInt32, motionFrame: Float = .nan) -> dmc_view {
        var view = dmc_view()
        view.yaw = camera.yaw; view.pitch = camera.pitch; view.zoom = camera.zoom
        view.render_flags = flags; view.pan_x = camera.panX; view.pan_y = camera.panY
        view.room_yaw = camera.roomYaw; view.motion_frame = motionFrame
        return view
    }

    /// The photo camera's picture of a view_width x view_height view (full size, the core's).
    func capturePhoto(viewWidth: Int, viewHeight: Int, camera: ViewCamera, flags: UInt32, motionFrame: Float) -> RGBAImage? {
        var v = view(of: camera, flags: flags, motionFrame: motionFrame)
        guard let image = dmc_render_capture(handle, false, Int32(viewWidth), Int32(viewHeight), &v, 60) else { return nil }
        defer { dmc_image_free(image) }
        return RGBAImage(copying: image.pointee)
    }

    /// One video frame of the view (dmc_video_frame) into `rgba`.
    func videoFrame(viewWidth: Int, viewHeight: Int, camera: ViewCamera, flags: UInt32, motionFrame: Float,
                    rgba: UnsafeMutablePointer<UInt8>, capacity: Int) -> Bool {
        var v = view(of: camera, flags: flags, motionFrame: motionFrame)
        return dmc_video_frame(handle, Int32(viewWidth), Int32(viewHeight), &v, rgba, capacity) != 0
    }

    /// A RAW (DNG) picture written to `fd`.
    func captureDNG(viewWidth: Int, viewHeight: Int, camera: ViewCamera, flags: UInt32, motionFrame: Float, fd: Int32) -> Bool {
        var v = view(of: camera, flags: flags, motionFrame: motionFrame)
        return dmc_capture_dng(handle, Int32(viewWidth), Int32(viewHeight), &v, 60, fd, nil)
    }

    /// Fly mode: enter from the view, one tick of the sticks, leave.
    func flyEnter(width: Int, height: Int, camera: ViewCamera, flags: UInt32) -> Bool {
        var v = view(of: camera, flags: flags)
        return dmc_fly_enter(handle, Int32(width), Int32(height), &v)
    }

    func flyTick(yaw: Float, pitch: Float, move: (Float, Float)?, look: (Float, Float)?, dt: Float) -> dmc_fly_step {
        dmc_fly_tick(handle, yaw, pitch, move != nil, move?.0 ?? 0, move?.1 ?? 0,
                     look != nil, look?.0 ?? 0, look?.1 ?? 0, 1, dt)
    }

    func flyLeave(yaw: Float, pitch: Float) { dmc_fly_leave(handle, yaw, pitch) }

    /// The viewfinder of a width x height frame (nil when the camera mode does not show one).
    func viewfinder(width: Int, height: Int, zoom: Float, recording: Bool, seconds: Int) -> Viewfinder? {
        var finder = dmc_viewfinder(handle, Int32(width), Int32(height), zoom, recording, Int32(seconds))
        defer { dmc_finder_free(&finder) }
        guard finder.shown else { return nil }
        var lines: [CGRect] = []
        for i in 0..<finder.line_count {
            let p = finder.lines + i * 4
            lines.append(CGRect(x: CGFloat(p[0]), y: CGFloat(p[1]), width: CGFloat(p[2] - p[0]), height: CGFloat(p[3] - p[1])))
        }
        return Viewfinder(frame: CGRect(x: CGFloat(finder.frame.0), y: CGFloat(finder.frame.1),
                                        width: CGFloat(finder.frame.2), height: CGFloat(finder.frame.3)),
                          lines: lines, status: finder.status.map { String(cString: $0) } ?? "")
    }

    /// The core's diagnostics text for the Info window.
    func diagnostics() -> String { takeString(dmc_diagnostics(handle)) ?? "" }
}

private func rows(of menu: OpaquePointer?) -> [MenuRow] {
    guard let menu else { return [] }
    return (0..<dmc_menu_count(menu)).compactMap { index in
        guard let item = dmc_menu_at(menu, index)?.pointee else { return nil }
        return MenuRow(id: Int(item.id), label: String(cString: item.label), icon: String(cString: item.icon))
    }
}

struct OpenFailure: Error {
    let message: String
}

/// One row of the core's settings (spider/viewer_settings): the same schema Android shows.
struct SettingRow: Identifiable, Hashable {
    let key: String
    let section: String
    let label: String
    let isSlot: Bool               // a piece the shell draws itself
    let choices: [String]
    let values: [Float]
    let fallback: Float
    let hint: String
    let roomOnly: Bool
    var id: String { key }
}

enum ReaderCore {
    /// The start screen's text, written by the core from its module registry.
    static func welcomeText(version: String) -> String {
        takeString(dmc_welcome_text(version, "macOS")) ?? "DMC Native Reader \(version)"
    }

    /// Applies the settings' defaults (nothing stored yet on this Mac).
    static func applyDefaultSettings() -> AppliedSettings { apply(stored: [:]) }

    /// The settings schema, described for this device.
    static func settingsSchema() -> [SettingRow] {
        guard let schema = dmc_settings_schema(true) else { return [] }
        defer { dmc_settings_free(schema) }
        var rows: [SettingRow] = []
        for index in 0..<dmc_settings_count(schema) {
            guard let row = dmc_settings_row(schema, index)?.pointee else { continue }
            var choices: [String] = []
            var values: [Float] = []
            for c in 0..<row.choice_count {
                choices.append(row.choices[c].map { String(cString: $0) } ?? "")
                values.append(row.values[c])
            }
            rows.append(SettingRow(key: String(cString: row.key), section: String(cString: row.section),
                                   label: String(cString: row.label),
                                   isSlot: row.control == Int32(DMC_SETTING_SLOT.rawValue),
                                   choices: choices, values: values, fallback: row.fallback,
                                   hint: row.hint.map { String(cString: $0) } ?? "", roomOnly: row.room_only))
        }
        return rows
    }

    /// The base room every model stands in, from a stNNN file; the core's detail or nil.
    static func loadRoom(name: String, bytes: Data) -> String? {
        bytes.withUnsafeBytes { raw in
            takeString(dmc_room_load(name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count))
        }
    }

    static func clearRoom() { dmc_room_clear() }

    /// A stage configuration (stNNNcfg.pac) for slot 0 (the base room) or 1 (the opened stage).
    static func loadStageConfig(name: String, bytes: Data, slot: Int) -> Outcome {
        var result = bytes.withUnsafeBytes { raw in
            dmc_stage_config_load(name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count, Int32(slot))
        }
        defer { dmc_outcome_free(&result) }
        return Outcome(ok: result.ok, step: result.step, message: String(cString: result.message),
                       marker: result.marker, title: String(cString: result.title), opened: nil)
    }

    /// A marker's card: title, rows and buttons (nil: no card).
    static func markerCard(_ marker: Int) -> MarkerCardInfo? {
        guard let card = dmc_marker_card(Int32(marker)) else { return nil }
        defer { dmc_card_free(card) }
        var cardRows: [(String, String)] = []
        for index in 0..<dmc_card_row_count(card) {
            var name: UnsafePointer<CChar>?
            var value: UnsafePointer<CChar>?
            if dmc_card_row(card, index, &name, &value), let name, let value {
                cardRows.append((String(cString: name), String(cString: value)))
            }
        }
        let buttons = rows(of: dmc_card_actions(card))
        return MarkerCardInfo(marker: marker, title: String(cString: dmc_card_title(card)),
                              rows: cardRows.map { MarkerCardInfo.Row(name: $0.0, value: $0.1) }, buttons: buttons)
    }

    static func runCardAction(marker: Int, id: Int) -> Outcome {
        var opened: OpaquePointer?
        var result = dmc_run_card_action(Int32(marker), Int32(id), &opened)
        defer { dmc_outcome_free(&result) }
        var outcome = Outcome(ok: result.ok, step: result.step, message: String(cString: result.message),
                              marker: result.marker, title: String(cString: result.title), opened: nil)
        if let opened { outcome.opened = ReaderSession(adopting: opened, title: outcome.title.isEmpty ? "File" : outcome.title) }
        return outcome
    }

    /// The photo camera's settings (the core's schema): id, section, label, hint, options, start.
    static func cameraSettings() -> [CameraSettingRow] {
        guard let list = dmc_camera_settings_list() else { return [] }
        defer { dmc_camera_settings_free(list) }
        return (0..<dmc_camera_settings_count(list)).compactMap { index in
            guard let s = dmc_camera_settings_at(list, index)?.pointee else { return nil }
            let options = (0..<s.option_count).map { s.options[$0].map { String(cString: $0) } ?? "" }
            return CameraSettingRow(key: Int(s.key), id: String(cString: s.id), section: String(cString: s.section),
                                    label: String(cString: s.label), hint: String(cString: s.hint),
                                    options: options, start: Int(s.start))
        }
    }

    static func setCameraChoices(_ choices: [Int]) {
        let values = choices.map { Int32($0) }
        values.withUnsafeBufferPointer { dmc_camera_set_choices($0.baseAddress, values.count) }
    }

    /// The camera bar's dials: title and value as a camera shows them.
    static func cameraDials() -> [(key: Int, title: String, value: String)] {
        guard let list = dmc_camera_dials_list() else { return [] }
        defer { dmc_camera_dials_free(list) }
        return (0..<dmc_camera_dials_count(list)).compactMap { index in
            guard let d = dmc_camera_dials_at(list, index)?.pointee else { return nil }
            return (Int(d.key), String(cString: d.title), String(cString: d.value))
        }
    }

    /// The stored values (key -> value) applied: what they mean for the view.
    static func apply(stored: [String: Float]) -> AppliedSettings {
        let keys = Array(stored.keys)
        let values = keys.map { stored[$0] ?? 0 }
        let cKeys = keys.map { strdup($0) }
        defer { cKeys.forEach { free($0) } }
        let applied = cKeys.map { UnsafePointer($0) }.withUnsafeBufferPointer { keyBuffer in
            values.withUnsafeBufferPointer { valueBuffer in
                dmc_apply_settings(keyBuffer.baseAddress, valueBuffer.baseAddress, keys.count)
            }
        }
        return AppliedSettings(renderFlags: applied.render_flags, maxSide: Int(applied.max_side),
                               shadows: applied.shadows)
    }
}

/// Render flag bits the shell adds per session (view_renderer.h RenderFlag).
enum RenderFlag {
    static let shadows: UInt32 = 1 << 6
    static let uvLayout: UInt32 = 1 << 5
    static let collision: UInt32 = 1 << 7          // attack hitboxes on the bones
    static let roomCollision: UInt32 = 1 << 14     // the stage's HITS walls and floors
}

/// The core's account of a collision: groups with a colour, heading and lines.
struct CollisionReport: Identifiable {
    struct Group: Identifiable {
        let id: Int
        let rgb: Int64
        let heading: String
        let lines: String
    }
    let id = UUID()
    let title: String
    let summary: String
    let groups: [Group]
}

/// Takes a string the C API allocated and frees it.
func takeString(_ pointer: UnsafeMutablePointer<CChar>?) -> String? {
    guard let pointer else { return nil }
    defer { dmc_string_free(pointer) }
    return String(cString: pointer)
}
