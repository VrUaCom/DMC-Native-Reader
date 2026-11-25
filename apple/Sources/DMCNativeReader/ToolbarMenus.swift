// The window's toolbar, grouped: what belongs together sits in one menu, so
// that everything stays visible in a narrow window. The same menus are in the
// menu bar (DMCNativeReaderApp.commands).
import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The panels beside the view: one at a time, a second press closes it.
enum SideTool { case textures, settings, info }

extension ViewerModel {
    func toggle(_ tool: SideTool) {
        let wasShown = isShown(tool)
        texturesShown = false
        settingsShown = false
        infoShown = false
        guard !wasShown else { return }
        switch tool {
        case .textures: texturesShown = true
        case .settings: settingsShown = true
        case .info: infoShown = true
        }
    }

    func isShown(_ tool: SideTool) -> Bool {
        switch tool {
        case .textures: return texturesShown
        case .settings: return settingsShown
        case .info: return infoShown
        }
    }

    /// A 3D session: the view tools apply.
    var has3D: Bool { session != nil && !isPicture }

    /// Asks where to write and hands the place over.
    func askSave(suffix: String, types: [UTType], _ done: (URL) -> Void) {
        let panel = NSSavePanel()
        if !types.isEmpty { panel.allowedContentTypes = types }
        panel.nameFieldStringValue = (session?.title ?? "view") + suffix
        if panel.runModal() == .OK, let url = panel.url { done(url) }
    }
}

/// What the model does: motions, the character's kit, effects, actions, sounds.
struct ModelMenuItems: View {
    @ObservedObject var model: ViewerModel

    var body: some View {
        Button("Load motion…") { OpenPanel.choose { model.loadMotion(url: $0) } }
            .disabled(!model.has3D)
        if !model.kit.isEmpty {
            Divider()
            kit("Weapons", kind: "weapon")
            kit("Motions by weapon", kind: "motions")
            kit("Devil Trigger", kind: "dt")
            Button("Add the whole kit (no DT)") { model.addWholeKit() }
            Button("Choose a PAC file…") { model.choosePacs() }
        }
        if !model.actions.isEmpty || !model.moreActions.isEmpty || !model.effects.isEmpty ||
            !model.classEvents.isEmpty || !model.sounds.isEmpty {
            Divider()
        }
        if !model.actions.isEmpty {
            ForEach(model.actions) { row in action(row) }
        }
        if !model.moreActions.isEmpty {
            Menu("Other actions") { ForEach(model.moreActions) { row in action(row) } }
        }
        if !model.effects.isEmpty {
            Menu("Effects") {
                ForEach(model.effects) { row in
                    Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.playEffect(row) }
                }
            }
        }
        if !model.classEvents.isEmpty {
            Menu("Events") {
                ForEach(Array(model.classEvents.enumerated()), id: \.offset) { i, title in
                    Button(title) { model.triggerClassEvent(i) }
                }
            }
        }
        if !model.sounds.isEmpty {
            Menu("Sounds") {
                ForEach(model.sounds) { row in Button(row.label) { model.playSound(row) } }
                Divider()
                Button("Stop") { model.stopSound() }
            }
        }
    }

    private func action(_ row: MenuRow) -> some View {
        Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.runCompanion(row) }
    }

    /// One part of the kit: a tick per PAC added, dimmed when it is not beside the file.
    @ViewBuilder private func kit(_ title: String, kind: String) -> some View {
        let items = model.kit.filter { $0.kind == kind }
        if !items.isEmpty {
            Menu(title) {
                ForEach(items) { item in
                    Toggle(item.label + (item.url == nil ? "  (\(item.file) not beside it)" : "  — \(item.file)"),
                           isOn: Binding(get: { model.isAdded(item) }, set: { _ in model.toggle(item) }))
                        .disabled(item.url == nil)
                }
            }
        }
    }
}

/// Where the model stands: the base room, stage files, hitboxes and collision.
struct SceneMenuItems: View {
    @ObservedObject var model: ViewerModel

    var body: some View {
        Button("Load base room…") { OpenPanel.choose { model.loadRoom(url: $0) } }
        if let room = model.roomName {
            Button("Clear base room (\(room))") { model.clearRoom() }
            Button("Next spot") { model.nextSpot() }.keyboardShortcut("n", modifiers: [])
            let spawns = model.spawnPoints
            if !spawns.isEmpty {
                Menu("Spawn points") {
                    ForEach(Array(spawns.enumerated()), id: \.offset) { i, title in Button(title) { model.standOnSpawn(i) } }
                }
            }
        }
        if model.canBreakRoom {
            Toggle("Broken", isOn: Binding(get: { model.roomBroken }, set: { model.setRoomBroken($0) }))
        }
        Divider()
        Button("Load stage configuration…") { OpenPanel.choose { model.loadStageConfig(url: $0) } }
            .disabled(model.session == nil)
        Button(model.commonEffectsName.map { "Common effects: \($0)…" } ?? "Load common effects (static_effect.pac)…") {
            OpenPanel.choose { model.loadCommonEffects(url: $0) }
        }
        if model.commonEffectsName != nil {
            Button("Clear common effects") { model.clearCommonEffects() }
        }
        if model.canShowHitboxes {
            Divider()
            Button("Next hitboxes") { model.cycleHitboxes() }.keyboardShortcut("h", modifiers: [])
            Menu("Hitboxes") {
                Button("All attacks") { model.showHitboxes(-1) }
                ForEach(model.attackIDs, id: \.self) { id in
                    Button("Attack \(id)") { model.showHitboxes(id) }
                }
            }
            Button("Hide hitboxes") { model.showHitboxes(nil) }.disabled(model.hitboxAttack == nil)
            Button("Hitbox report…") { model.showCollisionReport(attack: true) }
        }
        if model.canShowStageCollision {
            Divider()
            Toggle("Stage collision", isOn: Binding(get: { model.stageCollisionShown },
                                                    set: { _ in model.toggleStageCollision() }))
            Button("Stage collision report…") { model.showCollisionReport(attack: false) }
        }
    }
}

/// What the file holds: the model's lists, its UV maps, the benchmark.
struct InspectMenuItems: View {
    @ObservedObject var model: ViewerModel

    var body: some View {
        Group {
            Button("Meshes") { model.showInspection(2, title: "Meshes") }
            Button("Hierarchy") { model.showInspection(3, title: "Hierarchy") }
            Button("UV") { model.showInspection(1, title: "UV") }
            if model.canShowUV {
                Button("UV maps, one per texture") { model.openUVMaps() }
            }
            Divider()
            Button("Benchmark (10 s)") { model.runBenchmark() }.disabled(model.isPicture)
        }
        .disabled(model.session == nil)
    }
}

/// The view itself: reset, fly, the photo camera, pictures and video.
struct CameraMenuItems: View {
    @ObservedObject var model: ViewerModel

    var body: some View {
        Group {
            Button("Reset view") { model.resetCamera() }
            Toggle("Fly", isOn: Binding(get: { model.flying }, set: { _ in model.toggleFly() }))
                .keyboardShortcut("f", modifiers: [])
            Toggle("Viewfinder (camera mode)", isOn: Binding(get: { model.cameraModeChoice },
                                                             set: { model.setCameraMode($0) }))
                .keyboardShortcut("v", modifiers: [])
            Divider()
            Button("Take picture (PNG)…") {
                model.askSave(suffix: " photo.png", types: [.png]) { model.savePhoto(to: $0) }
            }
            Button("Take RAW picture (DNG)…") {
                model.askSave(suffix: ".dng", types: []) { model.saveDNG(to: $0) }
            }
            Button(model.recording ? "Stop recording" : "Record video…") {
                if model.recording { model.toggleRecording(); return }
                model.askSave(suffix: ".mov", types: [.quickTimeMovie]) { model.toggleRecording(to: $0) }
            }
        }
        .disabled(!model.has3D)
        Divider()
        Button("Export the view as PNG…") {
            model.askSave(suffix: ".png", types: [.png]) { model.exportPNG(to: $0) }
        }
        .disabled(model.session == nil)
    }
}

extension ContentView {
    @ToolbarContentBuilder
    var toolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .navigation) {
            Button { model.goBack() } label: { Label("Back", systemImage: "chevron.backward") }
                .disabled(!model.canGoBack)
            Button { OpenPanel.choose { model.open(url: $0) } } label: { Label("Open", systemImage: "folder") }
                .help("Open a file (⌘O)")
        }
        ToolbarItemGroup {
            if model.dualPreview {
                Picker("Picture", selection: Binding(get: { model.infoPreviewShown },
                                                     set: { model.setInfoPreview($0) })) {
                    Text("Texture").tag(false)
                    Text("Info").tag(true)
                }
                .pickerStyle(.segmented)
                .help("The texture, or the core's Info picture of it")
            }
            Menu { ModelMenuItems(model: model) } label: { Label("Model", systemImage: "figure.stand") }
                .help("Model: motions, weapons, effects, events, sounds and other actions")
                .disabled(model.session == nil)
            Menu { SceneMenuItems(model: model) } label: { Label("Scene", systemImage: "cube.transparent") }
                .help("Scene: base room, stage files, hitboxes and collision")
            Menu { InspectMenuItems(model: model) } label: { Label("Inspect", systemImage: "list.bullet.rectangle") }
                .help("Inspect: meshes, hierarchy, UV, benchmark")
                .disabled(model.session == nil)
            Menu { CameraMenuItems(model: model) } label: {
                Label("Camera", systemImage: model.flying ? "airplane" : (model.recording ? "record.circle" : "camera"))
            }
            .help("Camera: reset, fly, viewfinder, pictures and video")
            .disabled(model.session == nil)
        }
        ToolbarItemGroup {
            panelButton(.textures, "Textures", "paintpalette",
                        "Textures: drop a PNG or DDS of your own on one to see it on the model (nothing is written)")
                .disabled(!model.has3D)
            panelButton(.settings, "Settings", "gearshape",
                        "Settings: graphics, lines, animation, base room and the photo camera")
            panelButton(.info, "Info", "info.circle", "Info and file health")
                .disabled(model.session == nil)
                .overlay(alignment: .topTrailing) {
                    if model.healthLevel > 0 {
                        Circle()
                            .fill(model.healthLevel >= 2 ? Color.red : Color.orange)
                            .frame(width: 7, height: 7)
                    }
                }
        }
    }

    /// A panel's button, lit while its panel is open.
    private func panelButton(_ tool: SideTool, _ title: String, _ image: String, _ help: String) -> some View {
        Toggle(isOn: Binding(get: { model.isShown(tool) }, set: { _ in model.toggle(tool) })) {
            Label(title, systemImage: image)
        }
        .toggleStyle(.button)
        .help(help)
    }
}

/// The opened archive's in-game looks. Names made of the same number of
/// " · " parts (Vergil: weapons · DT) get one switch per part: "X on / X off"
/// a switch, a few short names segments, more a menu. Otherwise one list.
struct VariantBar: View {
    @ObservedObject var model: ViewerModel

    private var parts: [[String]] { model.variants.map { $0.components(separatedBy: " · ") } }

    private var columns: Int {
        let counts = Set(parts.map(\.count))
        guard counts.count == 1, let n = counts.first, n > 1 else { return 1 }
        return n
    }

    var body: some View {
        HStack(spacing: 14) {
            ForEach(0..<columns, id: \.self) { column in control(column) }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    @ViewBuilder private func control(_ column: Int) -> some View {
        let names = options(column)
        if let name = onOff(names) {
            Toggle(name, isOn: Binding(get: { current(column).hasSuffix(" on") },
                                       set: { choose(column: column, value: name + ($0 ? " on" : " off")) }))
                .toggleStyle(.switch)
                .controlSize(.small)
                .fixedSize()
                .help("\(name): on or off, the rest of the look stays")
        } else if names.count > 1 {
            let pick = Picker("", selection: Binding(get: { current(column) },
                                                     set: { choose(column: column, value: $0) })) {
                ForEach(names, id: \.self) { Text($0).tag($0) }
            }
            .labelsHidden()
            .fixedSize()
            // Segments when all of them fit in the window, else a menu.
            if names.count <= 6 && names.joined().count <= 70 {
                ViewThatFits(in: .horizontal) {
                    pick.pickerStyle(.segmented)
                    pick.pickerStyle(.menu)
                }
            } else {
                pick.pickerStyle(.menu)
            }
        }
    }

    /// "DT" for {"DT off", "DT on"}.
    private func onOff(_ names: [String]) -> String? {
        guard names.count == 2, let on = names.first(where: { $0.hasSuffix(" on") }) else { return nil }
        let name = String(on.dropLast(3))
        return names.contains(name + " off") ? name : nil
    }

    private func text(_ index: Int, _ column: Int) -> String {
        columns == 1 ? model.variants[index] : parts[index][column]
    }

    private func options(_ column: Int) -> [String] {
        var seen: [String] = []
        for i in model.variants.indices where !seen.contains(text(i, column)) { seen.append(text(i, column)) }
        return seen
    }

    private func current(_ column: Int) -> String {
        model.variants.indices.contains(model.variantIndex) ? text(model.variantIndex, column) : ""
    }

    /// The look with this part changed and the others kept; else the first with it.
    private func choose(column: Int, value: String) {
        guard model.variants.indices.contains(model.variantIndex) else { return }
        if columns == 1 {
            if let i = model.variants.firstIndex(of: value) { model.selectVariant(i) }
            return
        }
        var wanted = parts[model.variantIndex]
        wanted[column] = value
        if let exact = parts.firstIndex(of: wanted) {
            model.selectVariant(exact)
        } else if let any = parts.firstIndex(where: { $0[column] == value }) {
            model.selectVariant(any)
        }
    }
}
