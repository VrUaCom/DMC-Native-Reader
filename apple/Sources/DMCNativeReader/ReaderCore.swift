// The reader's C API (dmc_reader.h) in Swift terms. Every decision and every
// line of text comes from the C++ core; this file only converts types and
// owns the handles. No Apple framework is used here, so it also compiles on
// Linux, where its C interop is checked.
import CDMCReader
import Foundation

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

    deinit { dmc_close(handle) }

    /// Opens `bytes` named `name`; on rejection the core's reason comes back as text.
    static func open(name: String, bytes: Data) -> Result<ReaderSession, OpenFailure> {
        var result = bytes.withUnsafeBytes { raw -> dmc_open_result in
            let base = raw.bindMemory(to: UInt8.self).baseAddress
            return dmc_open(name, base, raw.count)
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

    var childCount: Int { Int(dmc_child_count(handle)) }

    func childTitle(_ index: Int) -> String {
        takeString(dmc_child_title(handle, index)) ?? "#\(index)"
    }

    var state: SessionState { SessionState(rawValue: dmc_state_flags(handle)) }

    /// Info, written by the core: structure, evidence, navigation, staged files.
    func infoText(navigationDepth: Int, backTitle: String) -> String {
        takeString(dmc_info_text(handle, title, navigationDepth, backTitle, nil, 0)) ?? title
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

    /// A frame of the 3D view, or nil when the session draws none.
    func render(width: Int, height: Int, camera: ViewCamera, flags: UInt32) -> RGBAImage? {
        guard width > 0, height > 0 else { return nil }
        var view = dmc_view()
        view.yaw = camera.yaw
        view.pitch = camera.pitch
        view.zoom = camera.zoom
        view.render_flags = flags
        view.pan_x = camera.panX
        view.pan_y = camera.panY
        view.room_yaw = camera.roomYaw
        view.motion_frame = Float.nan
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let status = pixels.withUnsafeMutableBufferPointer { buffer in
            dmc_render(handle, Int32(width), Int32(height), &view, buffer.baseAddress, buffer.count)
        }
        return status == 0 ? nil : RGBAImage(width: width, height: height, pixels: pixels)
    }
}

struct OpenFailure: Error {
    let message: String
}

enum ReaderCore {
    /// The start screen's text, written by the core from its module registry.
    static func welcomeText(version: String) -> String {
        takeString(dmc_welcome_text(version, "macOS")) ?? "DMC Native Reader \(version)"
    }

    /// Applies the settings' defaults (nothing stored yet on this Mac).
    static func applyDefaultSettings() -> AppliedSettings {
        let applied = dmc_apply_settings(nil, nil, 0)
        return AppliedSettings(renderFlags: applied.render_flags, maxSide: Int(applied.max_side),
                               shadows: applied.shadows)
    }
}

/// Render flag bits the shell adds per session (view_renderer.h RenderFlag).
enum RenderFlag {
    static let shadows: UInt32 = 1 << 6
    static let uvLayout: UInt32 = 1 << 5
}

/// Takes a string the C API allocated and frees it.
private func takeString(_ pointer: UnsafeMutablePointer<CChar>?) -> String? {
    guard let pointer else { return nil }
    defer { dmc_string_free(pointer) }
    return String(cString: pointer)
}
