// The companion menu's rows the core leaves to the shell (a file to pick or to
// save, a format to choose, parts to place), as Android's MainActivity
// runCompanion: the shell asks, the core does the work.
import AppKit
import CDMCReader
import Foundation
import UniformTypeIdentifiers

/// Companion ids (spider/companion_menu.h) the shell answers itself.
enum CompanionID {
    static let openFile = 1, textureFormat = 2, saveFile = 3, addPac = 4, browseAssembledPac = 5
    static let browseStagePac = 6, stageEffects = 7, addModParts = 8, compositeParts = 9
    static let placeModPart = 10, resetModPlacement = 11, attachPtx = 12
    static let stageMotion = 13, stageTexture = 14, stagePhysics = 15, stageCloth = 16, stageOther = 17
    static let stageConfigRemove = 20, diagnostics = 21, photo = 26, video = 27, cameraSettings = 28
    static let effects = 30, stageOpsRoom = 31
    static let settings = 100, gestures = 101, roomToggle = 102, nextRoomSpot = 103
}

/// One part of a composite model.
struct PartInfo: Identifiable, Hashable {
    let id: Int
    let name: String
    let state: String
    let nodes: [String]
    let defaultAttachment: Int
}

extension ReaderSession {
    /// A file as a source for assembling or composing (no open_resource decisions).
    static func openSource(name: String, bytes: Data) -> ReaderSession? {
        let handle = bytes.withUnsafeBytes { raw in
            dmc_open_source(name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count)
        }
        return handle.map { ReaderSession(adopting: $0, title: name) }
    }

    private static func withPointers<T>(_ sessions: [ReaderSession], _ names: [String],
                                        _ body: ([OpaquePointer?], [UnsafePointer<CChar>?]) -> T) -> T {
        let handles: [OpaquePointer?] = sessions.map { $0.handle }
        let cNames = names.map { strdup($0) }
        defer { cNames.forEach { free($0) } }
        return body(handles, cNames.map { UnsafePointer($0) })
    }

    /// MODs as one composite model; part 0 is the host.
    static func compose(_ parts: [ReaderSession], names: [String], title: String) -> ReaderSession? {
        let made = withPointers(parts, names) { handles, cNames in
            handles.withUnsafeBufferPointer { h in
                cNames.withUnsafeBufferPointer { n in dmc_compose(h.baseAddress, n.baseAddress, parts.count) }
            }
        }
        return made.map { ReaderSession(adopting: $0, title: title) }
    }

    /// A character's PACs as one scene.
    static func assemble(_ archives: [ReaderSession], names: [String], variant: Int, title: String) -> ReaderSession? {
        let made = withPointers(archives, names) { handles, cNames in
            handles.withUnsafeBufferPointer { h in
                cNames.withUnsafeBufferPointer { n in dmc_assemble(h.baseAddress, n.baseAddress, archives.count, variant) }
            }
        }
        return made.map { ReaderSession(adopting: $0, title: title) }
    }

    static func strings(_ list: OpaquePointer?) -> [String] {
        guard let list else { return [] }
        defer { dmc_strings_free(list) }
        return (0..<dmc_strings_count(list)).map { dmc_strings_at(list, $0).map { String(cString: $0) } ?? "" }
    }

    /// The classes an archive holds (em000.pac: several enemies), by its name.
    static func variantNames(_ archive: String) -> [String] { strings(dmc_variant_names(archive)) }

    /// The formats textures can be re-encoded to: the core's names and the labels shown.
    static var textureFormats: [(name: String, label: String)] {
        let names = strings(dmc_texture_format_names())
        let labels = strings(dmc_texture_format_labels())
        return names.count == labels.count ? Array(zip(names, labels)) : []
    }

    /// The model with its textures re-encoded to `format`; the core's report either way.
    func reencode(format: String, dx10: Bool) -> (ReaderSession?, String) {
        var detail: UnsafeMutablePointer<CChar>?
        let made = dmc_reencode_textures(handle, format, dx10, &detail)
        let text = takeString(detail) ?? ""
        return (made.map { ReaderSession(adopting: $0, title: "\(title) · \(format.uppercased())") }, text)
    }

    /// The file the session was built from, when it may be saved (assembled, re-encoded).
    func source() -> (name: String, data: Data)? {
        var name: UnsafePointer<CChar>?
        var bytes: UnsafePointer<UInt8>?
        var size = 0
        guard dmc_source(handle, &name, &bytes, &size), let bytes, size > 0 else { return nil }
        return (name.map { String(cString: $0) } ?? "resource.bin", Data(bytes: bytes, count: size))
    }

    func attachPTX(part: Int, name: String, data: Data) -> Bool {
        data.withUnsafeBytes { raw in
            dmc_attach_ptx(handle, Int32(part), name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count)
        }
    }

    func parts() -> [PartInfo] {
        guard let list = dmc_parts_list(handle) else { return [] }
        defer { dmc_parts_free(list) }
        return (0..<dmc_parts_count(list)).compactMap { index in
            guard let p = dmc_parts_at(list, index)?.pointee else { return nil }
            let nodes = (0..<p.node_count).map { p.node_names[$0].map { String(cString: $0) } ?? "#\($0)" }
            return PartInfo(id: index, name: String(cString: p.name), state: String(cString: p.state),
                            nodes: nodes, defaultAttachment: Int(p.default_attachment))
        }
    }

    func attachPart(host: Int, child: Int, joint: Int) -> String {
        takeString(dmc_part_attach(handle, host, child, UInt32(max(0, joint)))) ?? ""
    }

    func resetPart(_ child: Int) -> String { takeString(dmc_part_reset(handle, child)) ?? "" }
}

extension ViewerModel {
    /// A companion row: the shell's own steps, or the core's action.
    func runCompanion(_ row: MenuRow) {
        switch row.id {
        case CompanionID.openFile, CompanionID.stageOpsRoom:
            OpenPanel.choose { [weak self] in self?.open(url: $0) }
        case CompanionID.textureFormat: askTextureFormat = true
        case CompanionID.saveFile: saveSource()
        case CompanionID.addPac: pickFiles { [weak self] in self?.addArchives($0) }
        case CompanionID.browseAssembledPac, CompanionID.browseStagePac: browseSourceArchive()
        case CompanionID.stageEffects: OpenPanel.choose { [weak self] in self?.loadRoomEffects(url: $0) }
        case CompanionID.addModParts: pickFiles { [weak self] in self?.addModParts($0) }
        case CompanionID.compositeParts, CompanionID.placeModPart, CompanionID.resetModPlacement:
            partsShown = session?.parts() ?? []
        case CompanionID.attachPtx: OpenPanel.choose { [weak self] in self?.attachPTX(url: $0) }
        case CompanionID.stageMotion: stageFiles(role: "Motion")
        case CompanionID.stageTexture: stageFiles(role: "Texture")
        case CompanionID.stagePhysics: stageFiles(role: "Physics")
        case CompanionID.stageCloth: stageFiles(role: "Cloth")
        case CompanionID.stageOther: stageFiles(role: "Other")
        case CompanionID.diagnostics:
            diagnosticsTitle = "Diagnostics"
            diagnosticsText = session?.diagnostics() ?? ""
        case CompanionID.nextRoomSpot: nextSpot()
        case CompanionID.photo: SavePanel.choose(name: "photo.png", type: .png) { [weak self] in self?.savePhoto(to: $0) }
        case CompanionID.video:
            if recording { toggleRecording() } else {
                SavePanel.choose(name: "view.mov", type: .quickTimeMovie) { [weak self] in self?.toggleRecording(to: $0) }
            }
        case CompanionID.settings, CompanionID.cameraSettings:
            NSApp.sendAction(Selector(("showSettingsWindow:")), to: nil, from: nil)
        case CompanionID.gestures:
            message = "Drag turns, two-finger drag or ⌥-drag pans, pinch zooms, rotate twists; F flies (WASD, arrows)."
        default: run(action: row)
        }
    }

    private func pickFiles(_ done: @escaping ([URL]) -> Void) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        if panel.runModal() == .OK { done(panel.urls) }
    }

    private func read(_ url: URL) -> Data? {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        return try? Data(contentsOf: url, options: .mappedIfSafe)
    }

    /// Saves the file the session was built from (an assembled or re-encoded one).
    func saveSource() {
        guard let source = session?.source() else {
            message = "Nothing to save: only an assembled or re-encoded file is saved."
            return
        }
        SavePanel.choose(name: source.name, type: .data) { [weak self] url in
            do {
                try source.data.write(to: url)
                self?.message = "Saved \(url.lastPathComponent)."
            } catch {
                self?.message = "Could not save the file: \(error.localizedDescription)"
            }
        }
    }

    /// Textures re-encoded to `format`: the result opens on top (Back returns).
    func reencodeTextures(format: String, dx10: Bool) {
        guard let session else { return }
        message = "Re-encoding textures to \(format.uppercased())…"
        Task { @MainActor [weak self] in
            let (made, detail) = await Task.detached { session.reencode(format: format, dx10: dx10) }.value
            guard let self else { return }
            guard let made else {
                self.message = "Format change failed: \(detail)"
                return
            }
            self.stack.append(session)
            self.show(made)
            let first = detail.split(separator: "\n").first.map(String.init) ?? detail
            self.message = first + " — Actions → Save file…"
        }
    }

    /// The opened character's PAC with more PACs (weapons, variants) as one scene.
    private func addArchives(_ urls: [URL]) {
        guard sourceFile != nil else { message = "Open a character's PAC first."; return }
        addedPacs += urls.compactMap { url in read(url).map { (url.lastPathComponent, $0) } }
        if let session { stack.append(session) }
        reassemble()
    }

    /// Shows the in-game look `index` of the opened archive (dmc_variant_names).
    func selectVariant(_ index: Int) {
        guard variants.indices.contains(index), index != variantIndex else { return }
        variantIndex = index
        reassemble()
    }

    /// The opened character's PAC, the added PACs and the chosen look as one scene.
    private func reassemble() {
        guard let current = sourceFile else { return }
        var sources: [ReaderSession] = []
        var names: [String] = []
        for (name, data) in [(current.name, current.data)] + addedPacs.map({ ($0.name, $0.data) }) {
            guard let source = ReaderSession.openSource(name: name, bytes: data) else {
                message = "\(name) is not an archive the core reads."
                return
            }
            sources.append(source)
            names.append(name)
        }
        let look = variants.indices.contains(variantIndex) ? " · " + variants[variantIndex] : ""
        guard let scene = ReaderSession.assemble(sources, names: names, variant: variantIndex,
                                                 title: names.joined(separator: " + ") + look) else {
            message = "These archives could not be assembled."
            return
        }
        show(scene)
    }

    /// The opened MOD with more MODs as one composite model (the opened one hosts).
    private func addModParts(_ urls: [URL]) {
        guard let current = sourceFile else { message = "Open a MOD first."; return }
        var parts: [ReaderSession] = []
        var names: [String] = []
        for (name, data) in [(current.name, current.data)] + urls.compactMap({ url in read(url).map { (url.lastPathComponent, $0) } }) {
            guard let part = ReaderSession.openSource(name: name, bytes: data) else {
                message = "\(name) is not a model the core reads."
                return
            }
            parts.append(part)
            names.append(name)
        }
        guard parts.count >= 2,
              let model = ReaderSession.compose(parts, names: names, title: names.joined(separator: " + ")) else {
            message = "These models could not be put together."
            return
        }
        if let session { stack.append(session) }
        show(model)
        partsShown = model.parts()
    }

    /// The opened archive's own files, not the scene assembled from it.
    private func browseSourceArchive() {
        guard let current = sourceFile, let raw = ReaderSession.openSource(name: current.name, bytes: current.data) else {
            message = "The archive's files are not available."
            return
        }
        if let session { stack.append(session) }
        show(raw)
    }

    private func attachPTX(url: URL) {
        guard let session, let data = read(url) else { return }
        if session.attachPTX(part: -1, name: url.lastPathComponent, data: data) {
            message = "\(url.lastPathComponent) attached."
            refreshAfterChange()
        } else {
            message = "\(url.lastPathComponent) could not be attached: see Info."
        }
    }

    func loadRoomEffects(url: URL) {
        guard let data = read(url) else { return }
        let slot = session.map { Int(dmc_stage_slot($0.handle)) } ?? 0
        let text = data.withUnsafeBytes { raw in
            takeString(dmc_room_effects_load(url.lastPathComponent, raw.bindMemory(to: UInt8.self).baseAddress,
                                             raw.count, Int32(slot)))
        } ?? ""
        message = text.isEmpty ? "\(url.lastPathComponent): no effects read." : text
        if !text.isEmpty, slot == 0 { baseEffectsName = url.lastPathComponent }
        refreshAfterChange()
    }

    /// Files staged beside the model (motions, textures, physics, cloth): listed in Info.
    private func stageFiles(role: String) {
        pickFiles { [weak self] urls in
            guard let self else { return }
            self.staged.append(contentsOf: urls.map { (role: role, name: $0.lastPathComponent) })
            if role == "Motion" {
                self.stagedMotions.append(contentsOf: urls)
                self.motionCards = self.session?.motionCards(staged: self.stagedMotionNames) ?? []
            }
            self.message = "\(urls.count) \(role.lowercased()) file(s) staged: see Info."
        }
    }

    func attachPart(host: Int, child: Int, joint: Int) {
        guard let session else { return }
        message = "Part \(child): " + session.attachPart(host: host, child: child, joint: joint)
        partsShown = session.parts()
        requestFrame()
    }

    func resetPart(_ child: Int) {
        guard let session else { return }
        message = "Part \(child): " + session.resetPart(child)
        partsShown = session.parts()
        requestFrame()
    }
}

enum SavePanel {
    static func choose(name: String, type: UTType, _ done: @escaping (URL) -> Void) {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = name
        panel.allowedContentTypes = [type]
        if panel.runModal() == .OK, let url = panel.url { done(url) }
    }
}
