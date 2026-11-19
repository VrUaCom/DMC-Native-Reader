// The photo camera at the desk, as on the phone: a click focuses there, the
// ring and the AF lock from the keyboard, two fingers turn the polarizer (or
// roll the camera), L levels it, a dial clicked steps its setting.
import CDMCReader
import Foundation

extension ViewerModel {
    var inCameraMode: Bool { !isPicture && session != nil && dmc_camera_mode() }

    /// A click in camera mode: focus on the point (the core's line).
    func focus(at point: CGPoint) {
        guard let session else { return }
        let (width, height) = frameSize()
        let x = Float(point.x / max(1, viewSize.width)) * Float(width)
        let y = Float(point.y / max(1, viewSize.height)) * Float(height)
        var view = session.view(of: camera, flags: settings.renderFlags)
        let line = takeString(dmc_camera_focus_at(session.handle, Int32(width), Int32(height), &view, x, y)) ?? ""
        message = line.isEmpty ? "Nothing in focus there." : line
        refreshFinder()
        requestFrame()
    }

    func focusRing(_ steps: Float) {
        if let line = takeString(dmc_camera_focus_ring(steps)), !line.isEmpty { message = line }
        refreshFinder()
        requestFrame()
    }

    func focusLock() {
        if let line = takeString(dmc_camera_focus_lock()), !line.isEmpty { message = line }
        refreshFinder()
    }

    /// Two fingers turned: the polarizer when it turns now, else the camera's roll.
    func cameraTwist(_ radians: Float) {
        if dmc_camera_polarizer_turnable() {
            let angle = dmc_camera_polarizer_turn(radians)
            message = String(format: "Polarizer %.0f°", Double(angle) * 180 / .pi)
        } else {
            let roll = dmc_camera_roll(radians)
            message = String(format: "Roll %.1f°", Double(roll) * 180 / .pi)
        }
        refreshFinder()
        requestFrame()
    }

    func levelCamera() {
        dmc_camera_level()
        message = "Level"
        refreshFinder()
        requestFrame()
    }

    /// A dial clicked: its setting steps to the next option.
    func stepDial(key: Int) {
        guard let row = cameraRows.first(where: { $0.key == key }), !row.options.isEmpty,
              let index = cameraRows.firstIndex(of: row) else { return }
        let current = index < cameraChoices.count ? cameraChoices[index] : row.start
        setCamera(row, option: (current + 1) % row.options.count)
    }

    /// The core's camera mode setting ("finder_mode"): on shows the viewfinder and dials.
    var cameraModeChoice: Bool {
        guard let index = cameraRows.firstIndex(where: { $0.id == "finder_mode" }) else { return false }
        return (index < cameraChoices.count ? cameraChoices[index] : cameraRows[index].start) == 1
    }

    func setCameraMode(_ on: Bool) {
        guard let row = cameraRows.first(where: { $0.id == "finder_mode" }) else { return }
        setCamera(row, option: on ? 1 : 0)
        message = on ? "Camera mode: click focuses, , . turn the ring, K locks, L levels, two fingers turn the polarizer"
                     : "Camera mode off"
    }

    /// A camera gesture on a cutscene whose cuts drive the view: the core hands
    /// the shot's camera over and the gesture goes on from it (Android's claimCamera).
    func claimCutsceneCamera() {
        guard let session, !isPicture, dmc_camera_follows_cuts(session.handle) else { return }
        var view: [Float] = [0, 0, 1]
        guard view.withUnsafeMutableBufferPointer({ dmc_cutscene_take_camera(session.handle, $0.baseAddress) }) else { return }
        var next = camera
        next.yaw = view[0]
        next.pitch = view[1]
        next.zoom = dmc_pinch_zoom(view[2], 1)
        next.panX = 0
        next.panY = 0
        camera = next
        message = "Free camera: Actions → Cutscene camera gives the view back to the cuts."
    }
}
