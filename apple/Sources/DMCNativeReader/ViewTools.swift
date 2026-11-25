// The rest of Android's view tools, through the core: a click picks what is
// under the pointer (and stands the room's actor on the floor), the base
// room's spots and broken state, an enemy class's events, the inspection
// texts and the benchmark.
import CDMCReader
import Foundation

extension ReaderSession {
    struct Pick {
        let kind: Int
        let joint: Int
        let jointName: String
        let marker: Int
        let markerTitle: String
    }

    func pick(width: Int, height: Int, camera: ViewCamera, flags: UInt32, x: Float, y: Float, place: Bool) -> Pick {
        var view = view(of: camera, flags: flags)
        var result = dmc_pick(handle, Int32(width), Int32(height), &view, x, y, place)
        defer { dmc_pick_result_free(&result) }
        return Pick(kind: Int(result.kind), joint: Int(result.joint),
                    jointName: result.joint_name.map { String(cString: $0) } ?? "",
                    marker: Int(result.marker), markerTitle: result.marker_title.map { String(cString: $0) } ?? "")
    }

    func classEvents() -> [String] { ReaderSession.strings(dmc_class_events(handle)) }
    func triggerClassEvent(_ index: Int) -> Bool { dmc_trigger_class_event(handle, index) }

    /// Inspection text: 1 UV, 2 meshes, 3 hierarchy.
    func inspect(_ topic: Int) -> String { takeString(dmc_inspect(handle, Int32(topic))) ?? "" }

    func benchmark(width: Int, height: Int, camera: ViewCamera, flags: UInt32, seconds: Float, settings: String) -> String {
        var view = view(of: camera, flags: flags)
        return takeString(dmc_benchmark(handle, Int32(width), Int32(height), &view, .nan, seconds, settings)) ?? ""
    }
}

extension ViewerModel {
    /// A click at `point` (view points): the joint or marker under it; with the
    /// room shown, a click on its floor stands the actor there.
    func click(at point: CGPoint) {
        guard let session, !isPicture else { return }
        let (width, height) = frameSize()
        let x = Float(point.x / max(1, viewSize.width)) * Float(width)
        let y = Float(point.y / max(1, viewSize.height)) * Float(height)
        let pick = session.pick(width: width, height: height, camera: camera, flags: settings.renderFlags,
                                x: x, y: y, place: roomName != nil)
        switch Int32(pick.kind) {
        case Int32(DMC_PICK_PLACED.rawValue):
            message = "Placed on the floor."
            requestFrame()
        case Int32(DMC_PICK_MODEL.rawValue):
            if pick.marker >= 0 {
                markerCard = ReaderCore.markerCard(pick.marker)
            } else if !pick.jointName.isEmpty {
                message = "Joint \(pick.joint): \(pick.jointName)"
            }
        default:
            if pick.marker >= 0 { markerCard = ReaderCore.markerCard(pick.marker) }
        }
    }

    // MARK: Base room

    var spawnPoints: [String] { ReaderSession.strings(dmc_base_spawn_points()) }
    var canBreakRoom: Bool { session?.state.contains(.canBreakRoom) ?? false }

    func nextSpot() {
        var spot = dmc_next_base_spot()
        defer { dmc_base_spot_free(&spot) }
        if spot.index < 0 {
            message = "The room has no spot to stand on."
        } else {
            let title = spot.title.map { String(cString: $0) } ?? ""
            message = (spot.spawn ? "Spawn point " : "Spot ") + "\(spot.index + 1) of \(spot.count)"
                + (title.isEmpty ? "" : ": \(title)")
        }
        requestFrame()
    }

    func standOnSpawn(_ index: Int) {
        message = dmc_place_at_spawn(Int32(index)) ? "On spawn point \(index + 1)." : "That spawn point is not on the floor."
        requestFrame()
    }

    func setRoomBroken(_ broken: Bool) {
        roomBroken = broken
        dmc_room_set_broken(broken)
        refreshAfterChange()
    }

    // MARK: Class events, inspection, benchmark

    func triggerClassEvent(_ index: Int) {
        guard let session else { return }
        if session.triggerClassEvent(index) {
            message = "Event: \(classEvents.indices.contains(index) ? classEvents[index] : "#\(index)")"
            requestFrame()
        } else {
            message = "This event fires nothing here."
        }
    }

    func showInspection(_ topic: Int, title: String) {
        guard let session else { return }
        let text = session.inspect(topic)
        diagnosticsTitle = title
        diagnosticsText = text.isEmpty ? "Nothing to inspect." : text
    }

    func runBenchmark(seconds: Float = 10) {
        guard let session, !isPicture else { return }
        let (width, height) = frameSize()
        let camera = self.camera
        let flags = settings.renderFlags
        let line = "max side \(settings.maxSide), shadows \(settings.shadows ? "on" : "off")"
        message = "Benchmark: drawing for \(Int(seconds)) s…"
        Task { @MainActor [weak self] in
            let report = await Task.detached {
                session.benchmark(width: width, height: height, camera: camera, flags: flags,
                                  seconds: seconds, settings: line)
            }.value
            self?.message = nil
            self?.diagnosticsTitle = "Benchmark"
            self?.diagnosticsText = report.isEmpty ? "The view drew nothing." : report
        }
    }
}
