// DMC Native Reader for macOS: one window, the file's picture or 3D view,
// Info and file health; files open from ⌘O, by drag and drop, or from Finder.
import AppKit
import CDMCReader
import SwiftUI
import UniformTypeIdentifiers

@main
struct DMCNativeReaderApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = ViewerModel()

    var body: some Scene {
        WindowGroup {
            ContentView(model: model)
                .onAppear {
                    appDelegate.model = model
                    if model.crashReport == nil, let report = appDelegate.lastCrash {
                        model.crashReport = report
                        appDelegate.lastCrash = nil
                    }
                }
                .onOpenURL { model.open(url: $0) }
                // One window: a file opened from Finder goes to it, not to a new
                // window that would share its model.
                .handlesExternalEvents(preferring: ["*"], allowing: ["*"])
        }
        .handlesExternalEvents(matching: ["*"])
        Settings {
            TabView {
                SettingsView(model: model).tabItem { Label("View", systemImage: "eye") }
                CameraSettingsView(model: model).tabItem { Label("Camera", systemImage: "camera") }
            }
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open…") { OpenPanel.choose { model.open(url: $0) } }
                    .keyboardShortcut("o")
            }
            CommandGroup(replacing: .appInfo) {
                Button("About DMC Native Reader") { AboutPanel.show() }
            }
            CommandGroup(after: .toolbar) {
                Button("Zoom In") { model.zoom(by: 1.25) }.keyboardShortcut("+")
                Button("Zoom Out") { model.zoom(by: 0.8) }.keyboardShortcut("-")
                Button("Reset View") { model.resetCamera() }.keyboardShortcut("0")
                Divider()
            }
        }
    }
}

/// Files opened from Finder ("Open With") before or after the window exists.
final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: ViewerModel? {
        didSet { if let model, !waiting.isEmpty { waiting.forEach { url in Task { @MainActor in model.open(url: url) } }; waiting.removeAll() } }
    }
    private var waiting: [URL] = []
    /// The last run's crash report (CrashReports), until the window shows it.
    var lastCrash: String? = CrashReports.install()

    func applicationWillTerminate(_ notification: Notification) { CrashReports.left() }

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Started from Terminal (swift run) the app would stay in the background.
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        // The shell and the core it links must speak the same C API.
        if dmc_api_version() != DMC_API_VERSION {
            let alert = NSAlert()
            alert.messageText = "This app and its core do not match"
            alert.informativeText = "The core speaks C API \(dmc_api_version()), the app expects \(DMC_API_VERSION). Build both again with apple/build-mac.sh."
            alert.runModal()
        }
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let model else { waiting.append(contentsOf: urls); return }
        for url in urls { Task { @MainActor in model.open(url: url) } }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

enum OpenPanel {
    static func choose(_ done: @escaping (URL) -> Void) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.message = "A DMC resource: .mod, .scm, .pac, .ptx, .dds, .mot"
        if panel.runModal() == .OK, let url = panel.url { done(url) }
    }
}

struct ContentView: View {
    @ObservedObject var model: ViewerModel
    @State private var showInfo = false
    /// Where a gesture was at its last event: kept outside SwiftUI's state so
    /// that moving the camera never rebuilds the window's views.
    final class GestureTrack {
        var dragLast: CGSize?
        var zoomStart: Float?
        var twistLast: Double?
    }
    @State private var track = GestureTrack()
    @State private var spinTimer: Timer?

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                if model.variants.count > 1 { VariantBar(model: model) }
                main
                if let message = model.message {
                    // The status line: below the view, never over it.
                    Text(message)
                        .font(.callout)
                        .lineLimit(2)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 5)
                        .background(Color(nsColor: .windowBackgroundColor))
                }
            }
            if hasPanel {
                Divider()
                sidePanel
                    .frame(minWidth: 300, idealWidth: 340, maxWidth: 400)
            }
        }
        .frame(minWidth: 640, minHeight: 480)
        .navigationTitle(model.title)
        .toolbar { toolbar }
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            guard let provider = providers.first else { return false }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                if let url { Task { @MainActor in model.open(url: url) } }
            }
            return true
        }
        .onAppear { FlyKeys.install(model: model); model.loadCameraChoices() }
    }

    /// What used to be sheets over the window: one at a time, in a panel beside the view.
    private var hasPanel: Bool {
        model.settingsShown || model.crashReport != nil || showInfo || model.markerCard != nil || model.collisionReport != nil ||
            model.askTextureFormat || !model.partsShown.isEmpty || !model.diagnosticsText.isEmpty
    }

    @ViewBuilder private var sidePanel: some View {
        if let report = model.crashReport {
            TextSheet(title: "The reader closed by itself last time",
                      text: report + "\n\nThat file now opens as its description, once.")
                .environment(\.closePanel, ClosePanelAction { model.crashReport = nil })
        } else if let card = model.markerCard {
            MarkerCardSheet(model: model, card: card)
                .environment(\.closePanel, ClosePanelAction { model.markerCard = nil })
        } else if let report = model.collisionReport {
            CollisionReportSheet(report: report)
                .environment(\.closePanel, ClosePanelAction { model.collisionReport = nil })
        } else if model.askTextureFormat {
            TextureFormatSheet(model: model)
                .environment(\.closePanel, ClosePanelAction { model.askTextureFormat = false })
        } else if !model.partsShown.isEmpty {
            PartsSheet(model: model)
                .environment(\.closePanel, ClosePanelAction { model.partsShown = [] })
        } else if !model.diagnosticsText.isEmpty {
            TextSheet(title: model.diagnosticsTitle, text: model.diagnosticsText)
                .environment(\.closePanel, ClosePanelAction { model.diagnosticsText = "" })
        } else if showInfo {
            InfoSheet(model: model)
                .environment(\.closePanel, ClosePanelAction { showInfo = false })
        } else if model.settingsShown {
            SettingsPanel(model: model)
                .environment(\.closePanel, ClosePanelAction { model.settingsShown = false })
        }
    }

    private var main: some View {
        ZStack {
            Color(nsColor: NSColor(calibratedWhite: 0.09, alpha: 1)).ignoresSafeArea()
            if model.session == nil {
                start
            } else if model.children.isEmpty {
                viewer
            } else {
                HSplitView {
                    files.frame(minWidth: 200, idealWidth: 260, maxWidth: 420)
                    viewer.frame(minWidth: 320)
                }
            }
        }
    }

    private var start: some View {
        VStack(spacing: 16) {
            Text("Drop a DMC file here, or")
                .foregroundStyle(.secondary)
            Button("Open…") { OpenPanel.choose { model.open(url: $0) } }
                .keyboardShortcut("o")
            ScrollView {
                Text(model.welcome)
                    .font(.system(.footnote, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
                    .padding()
            }
            .frame(maxWidth: 640, maxHeight: 320)
        }
        .padding()
    }

    /// An archive's files: each opens as its own session; Back returns.
    private var files: some View {
        List(model.children) { child in
            Button { model.openChild(child.index) } label: {
                Text(child.title)
                    .font(.system(.callout, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.vertical, 1)
                    .background(model.selectedChild == child.index ? Color.accentColor.opacity(0.25) : Color.clear,
                                in: RoundedRectangle(cornerRadius: 4))
            }
            .buttonStyle(.plain)
        }
    }

    /// The view, with the animation strip under it when the session has motions.
    private var viewer: some View {
        VStack(spacing: 0) {
            viewport
            if !model.motionCards.isEmpty {
                Divider()
                motionStrip
            }
        }
    }

    /// The animation strip: one card per motion; a card's scripts are in its menu.
    private var motionStrip: some View {
        HStack(spacing: 8) {
            Button { model.togglePlaying() } label: {
                Image(systemName: model.playing ? "pause.fill" : "play.fill")
            }
            .help(model.playing ? "Pause" : "Play")
            .disabled(model.activeCard == nil)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(model.motionCards) { card in
                        if card.separatorBefore { Divider().frame(height: 22) }
                        motionCardButton(card)
                    }
                }
                .padding(.vertical, 6)
            }
        }
        .padding(.horizontal, 10)
        .frame(height: 44)
        .background(Color(nsColor: NSColor(calibratedWhite: 0.12, alpha: 1)))
    }

    @ViewBuilder
    private func motionCardButton(_ card: MotionCard) -> some View {
        let label = Text(card.stem.isEmpty ? card.name : card.stem)
            .font(.system(.caption, design: .monospaced))
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(model.activeCard == card.id ? Color.accentColor.opacity(0.35) : Color.white.opacity(0.06),
                        in: RoundedRectangle(cornerRadius: 6))
        if card.scripts.isEmpty {
            Button { model.play(card: card) } label: { label }
                .buttonStyle(.plain)
                .help(card.description)
                .disabled(card.libraryIndex < 0)
        } else {
            Menu {
                Button("Play \(card.name)") { model.play(card: card) }
                Divider()
                ForEach(card.scripts, id: \.self) { script in
                    Button(script.label) { model.play(card: card, script: script) }
                        .help(script.description)
                }
            } label: { label }
                .menuStyle(.borderlessButton)
                .fixedSize()
                .help(card.description)
        }
    }

    /// The viewfinder the core lays out in frame pixels, scaled to the view.
    private func viewfinderView(_ finder: Viewfinder, in size: CGSize) -> some View {
        let sx = size.width / max(1, model.finderFrameSize.width)
        let sy = size.height / max(1, model.finderFrameSize.height)
        return ZStack(alignment: .topLeading) {
            Path { path in
                path.addRect(CGRect(x: finder.frame.minX * sx, y: finder.frame.minY * sy,
                                    width: finder.frame.width * sx, height: finder.frame.height * sy))
                for line in finder.lines {
                    path.move(to: CGPoint(x: line.minX * sx, y: line.minY * sy))
                    path.addLine(to: CGPoint(x: line.maxX * sx, y: line.maxY * sy))
                }
            }
            .stroke(Color.white.opacity(0.8), lineWidth: 1)
            if !finder.status.isEmpty {
                Text(finder.status)
                    .font(.system(.caption, design: .monospaced))
                    .padding(4)
                    .background(.black.opacity(0.5))
                    .position(x: size.width / 2, y: max(12, finder.frame.minY * sy - 12))
            }
        }
        .allowsHitTesting(false)
    }

    /// A cutscene subtitle where and how the core lays it out (fractions of the view).
    private func subtitleView(_ s: Subtitle, in size: CGSize) -> some View {
        let color = { (c: (UInt8, UInt8, UInt8)) in Color(red: Double(c.0) / 255, green: Double(c.1) / 255, blue: Double(c.2) / 255) }
        return Text(s.text)
            .font(.system(size: max(10, CGFloat(s.size) * size.height), weight: .semibold))
            .multilineTextAlignment(.center)
            .foregroundStyle(color(s.front))
            .shadow(color: color(s.back), radius: 1.5)
            .shadow(color: color(s.back), radius: 1.5)
            .opacity(Double(s.alpha))
            .frame(width: size.width * 0.9)
            .position(x: CGFloat(s.centreX) * size.width, y: CGFloat(s.top) * size.height + CGFloat(s.size) * size.height)
            .allowsHitTesting(false)
    }

    private var viewport: some View {
        GeometryReader { geometry in
            ZStack {
                // Frames the GPU puts straight on the screen land here; the
                // picture covers it whenever a frame came back as pixels.
                PresentSurface(model: model)
                    .opacity(model.shownDirectly && !model.isPicture ? 1 : 0)
                    .allowsHitTesting(false)
                if model.shownDirectly && !model.isPicture {
                    EmptyView()
                } else if let image = model.image {
                    Image(decorative: image, scale: 1)
                        .resizable()
                        .interpolation(model.isPicture ? .none : .high)
                        .aspectRatio(contentMode: .fit)
                } else if !model.drawsNothing {
                    ProgressView()
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .overlay {
                if let opening = model.opening {
                    VStack(spacing: 8) {
                        ProgressView()
                        Text("Opening \(opening)…").font(.callout)
                    }
                    .padding(14)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
                }
            }
            .overlay(alignment: .topLeading) {
                if let subtitle = model.subtitle, !subtitle.text.isEmpty {
                    subtitleView(subtitle, in: geometry.size)
                }
            }
            .overlay { if let finder = model.viewfinder { viewfinderView(finder, in: geometry.size) } }
            .overlay(alignment: .bottom) {
                if !model.dials.isEmpty {
                    HStack(spacing: 14) {
                        ForEach(Array(model.dials.enumerated()), id: \.offset) { _, dial in
                            Button { model.stepDial(key: dial.key) } label: {
                                VStack(spacing: 1) {
                                    Text(dial.title).font(.caption2).foregroundStyle(.secondary)
                                    Text(dial.value).font(.system(.caption, design: .monospaced))
                                }
                            }
                            .buttonStyle(.plain)
                            .help("Click: the next \(dial.title)")
                        }
                    }
                    .padding(8)
                    .background(.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 8))
                    .padding(.bottom, 10)
                }
            }
            .contentShape(Rectangle())
            .gesture(orbit)
            .simultaneousGesture(LongPressGesture(minimumDuration: 0.5).sequenced(before: DragGesture(minimumDistance: 0))
                .onEnded { value in
                    if case .second(true, let drag?) = value { model.hold(at: drag.location) }
                })
            .simultaneousGesture(SpatialTapGesture().onEnded {
                if model.inCameraMode { model.focus(at: $0.location) } else { model.click(at: $0.location) }
            })
            .simultaneousGesture(magnify)
            .simultaneousGesture(twist)
            .onAppear { model.viewChanged(size: geometry.size, scale: NSScreen.main?.backingScaleFactor ?? 2) }
            .onChange(of: geometry.size) { size in
                model.viewChanged(size: size, scale: NSScreen.main?.backingScaleFactor ?? 2)
            }
        }
    }

    /// Drag turns the camera round the model (the core's grab-and-turn); a
    /// flick keeps it turning and slowing down.
    private var orbit: some Gesture {
        DragGesture(minimumDistance: 1)
            .onChanged { value in
                stopSpin()
                if track.dragLast == nil { model.claimCutsceneCamera() }
                let last = track.dragLast ?? .zero
                track.dragLast = value.translation
                model.camera.drag(dx: Float(value.translation.width - last.width),
                                  dy: Float(value.translation.height - last.height))
            }
            .onEnded { value in
                track.dragLast = nil
                // The flick's speed in points per second, from where the drag would have ended.
                let vx = Float(value.predictedEndTranslation.width - value.translation.width) * 4
                let vy = Float(value.predictedEndTranslation.height - value.translation.height) * 4
                var spin = dmc_spin()
                if dmc_fling(vx, vy, 1, &spin) { startSpin(spin) }
            }
    }

    private func startSpin(_ first: dmc_spin) {
        stopSpin()
        var spin = first
        var last = Date()
        spinTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { timer in
            Task { @MainActor in
                let now = Date()
                let dt = Float(now.timeIntervalSince(last))
                last = now
                if !model.camera.spin(&spin, dt: dt) { timer.invalidate() }
            }
        }
    }

    private func stopSpin() {
        spinTimer?.invalidate()
        spinTimer = nil
    }

    /// Pinch on the trackpad zooms (the core's limits).
    private var magnify: some Gesture {
        MagnificationGesture()
            .onChanged { scale in
                if track.zoomStart == nil { model.claimCutsceneCamera() }
                let start = track.zoomStart ?? model.camera.zoom
                if track.zoomStart == nil { track.zoomStart = start }
                model.camera.pinch(startZoom: start, scale: Float(scale))
            }
            .onEnded { _ in track.zoomStart = nil }
    }

    /// Two fingers turning on the trackpad turn the view.
    private var twist: some Gesture {
        RotationGesture()
            .onChanged { angle in
                let last = track.twistLast ?? 0
                track.twistLast = angle.radians
                let delta = dmc_angle_delta(Float(last), Float(angle.radians))
                if model.inCameraMode {
                    model.cameraTwist(delta)
                } else {
                    model.camera.twist(radians: delta, roomShown: false)
                }
            }
            .onEnded { _ in track.twistLast = nil }
    }

    @ToolbarContentBuilder
    private var toolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .navigation) {
            Button { model.goBack() } label: { Label("Back", systemImage: "chevron.backward") }
                .disabled(!model.canGoBack)
        }
        ToolbarItemGroup {
            Button { model.settingsShown.toggle() } label: { Label("Settings", systemImage: "gearshape") }
                .help("Settings: graphics, lines, animation, base room and the photo camera (the same as on Android)")
            Button { OpenPanel.choose { model.open(url: $0) } } label: { Label("Open", systemImage: "folder") }
            Button { model.resetCamera() } label: { Label("Reset view", systemImage: "scope") }
                .disabled(model.session == nil || model.isPicture)
            Button {
                OpenPanel.choose { model.loadMotion(url: $0) }
            } label: { Label("Load motion", systemImage: "figure.walk") }
                .help("Load a motion file (MOT, a motion PAC) onto the model")
                .disabled(model.session == nil || model.isPicture)
            Menu {
                ForEach(model.actions) { row in
                    Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.runCompanion(row) }
                }
                if !model.moreActions.isEmpty {
                    Menu("Other actions") {
                        ForEach(model.moreActions) { row in
                            Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.runCompanion(row) }
                        }
                    }
                }
            } label: { Label("Actions", systemImage: "ellipsis.circle") }
                .disabled(model.actions.isEmpty && model.moreActions.isEmpty)
            if !model.kit.isEmpty {
                Menu {
                    kitSection("Weapons — models on the body", kind: "weapon")
                    kitSection("Motions — animation banks (by weapon)", kind: "motions")
                    kitSection("Devil Trigger — replaces the body", kind: "dt")
                    Divider()
                    Button("Add the whole kit (no DT)") { model.addWholeKit() }
                    Button("Choose a PAC file…") { model.choosePacs() }
                } label: { Label("Character", systemImage: "figure.stand") }
                    .help("The character's weapons, motions and devil trigger: tick to add, untick to remove")
            }
            if !model.effects.isEmpty {
                Menu {
                    ForEach(model.effects) { row in
                        Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.playEffect(row) }
                    }
                } label: { Label("Effects", systemImage: "sparkles") }
            }
            if !model.classEvents.isEmpty {
                Menu {
                    ForEach(Array(model.classEvents.enumerated()), id: \.offset) { i, title in
                        Button(title) { model.triggerClassEvent(i) }
                    }
                } label: { Label("Events", systemImage: "bolt") }
            }
            if model.session != nil {
                Menu {
                    Button("UV") { model.showInspection(1, title: "UV") }
                    Button("Meshes") { model.showInspection(2, title: "Meshes") }
                    Button("Hierarchy") { model.showInspection(3, title: "Hierarchy") }
                    Divider()
                    Button("Benchmark (10 s)") { model.runBenchmark() }.disabled(model.isPicture)
                } label: { Label("Inspect", systemImage: "list.bullet.rectangle") }
            }
            if model.canShowUV {
                Button { model.openUVMaps() } label: { Label("UV maps", systemImage: "square.grid.3x3") }
                    .help("The model's UV layout, one map per texture")
            }
            if model.dualPreview {
                Picker("Picture", selection: Binding(get: { model.infoPreviewShown },
                                                     set: { model.setInfoPreview($0) })) {
                    Text("Texture").tag(false)
                    Text("Info").tag(true)
                }
                .pickerStyle(.segmented)
                .help("The texture, or the core's Info picture of it")
            }
            if model.canShowHitboxes || model.canShowStageCollision {
                Menu {
                    if model.canShowHitboxes {
                        Button("Next hitboxes") { model.cycleHitboxes() }.keyboardShortcut("h", modifiers: [])
                        Button("All attacks") { model.showHitboxes(-1) }
                        ForEach(model.attackIDs, id: \.self) { id in
                            Button("Attack \(id)") { model.showHitboxes(id) }
                        }
                        Button("Hide hitboxes") { model.showHitboxes(nil) }.disabled(model.hitboxAttack == nil)
                        Button("Hitbox report…") { model.showCollisionReport(attack: true) }
                    }
                    if model.canShowStageCollision {
                        Divider()
                        Button(model.stageCollisionShown ? "Hide stage collision" : "Show stage collision") {
                            model.toggleStageCollision()
                        }
                        Button("Stage collision report…") { model.showCollisionReport(attack: false) }
                    }
                } label: { Label("Collision", systemImage: "scope") }
            }
            if !model.sounds.isEmpty {
                Menu {
                    ForEach(model.sounds) { row in
                        Button(row.label) { model.playSound(row) }
                    }
                    Divider()
                    Button("Stop") { model.stopSound() }
                } label: { Label("Sounds", systemImage: "speaker.wave.2") }
            }
            Menu {
                Button("Load base room…") { OpenPanel.choose { model.loadRoom(url: $0) } }
                Button("Clear base room") { model.clearRoom() }
                    .disabled(model.roomName == nil)
                if model.roomName != nil {
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
                Divider()
                Button(model.commonEffectsName.map { "Common effects: \($0)…" } ?? "Load common effects (static_effect.pac)…") {
                    OpenPanel.choose { model.loadCommonEffects(url: $0) }
                }
                Button("Clear common effects") { model.clearCommonEffects() }.disabled(model.commonEffectsName == nil)
            } label: { Label(model.roomName ?? "Room", systemImage: "cube.transparent") }
            Menu {
                Toggle("Viewfinder (camera mode)", isOn: Binding(get: { model.cameraModeChoice },
                                                                 set: { model.setCameraMode($0) }))
                    .keyboardShortcut("v", modifiers: [])
                Divider()
                Button("Take picture (PNG)…") {
                    let panel = NSSavePanel()
                    panel.allowedContentTypes = [.png]
                    panel.nameFieldStringValue = (model.session?.title ?? "photo") + " photo.png"
                    if panel.runModal() == .OK, let url = panel.url { model.savePhoto(to: url) }
                }
                Button(model.recording ? "Stop recording" : "Record video…") {
                    if model.recording { model.toggleRecording(); return }
                    let panel = NSSavePanel()
                    panel.allowedContentTypes = [.quickTimeMovie]
                    panel.nameFieldStringValue = (model.session?.title ?? "view") + ".mov"
                    if panel.runModal() == .OK, let url = panel.url { model.toggleRecording(to: url) }
                }
                Button("Take RAW picture (DNG)…") {
                    let panel = NSSavePanel()
                    panel.nameFieldStringValue = (model.session?.title ?? "photo") + ".dng"
                    if panel.runModal() == .OK, let url = panel.url { model.saveDNG(to: url) }
                }
            } label: { Label("Camera", systemImage: "camera") }
                .disabled(model.session == nil || model.isPicture)
            Button { model.toggleFly() } label: {
                Label(model.flying ? "Stop flying" : "Fly", systemImage: model.flying ? "airplane.circle.fill" : "airplane")
            }
            .keyboardShortcut("f", modifiers: [])
            .disabled(model.session == nil || model.isPicture)
            Button {
                let panel = NSSavePanel()
                panel.allowedContentTypes = [.png]
                panel.nameFieldStringValue = (model.session?.title ?? "view") + ".png"
                if panel.runModal() == .OK, let url = panel.url { model.exportPNG(to: url) }
            } label: { Label("Export PNG", systemImage: "square.and.arrow.up") }
                .disabled(model.session == nil)
            Button { showInfo = true } label: {
                Label("Info", systemImage: "info.circle")
                    .overlay(alignment: .topTrailing) {
                        if model.healthLevel > 0 {
                            Circle()
                                .fill(model.healthLevel >= 2 ? Color.red : Color.orange)
                                .frame(width: 7, height: 7)
                        }
                    }
            }
        }
    }
}

extension ContentView {
    /// One part of the Character menu: a tick per PAC added, dimmed when it is not beside the file.
    @ViewBuilder func kitSection(_ title: String, kind: String) -> some View {
        let items = model.kit.filter { $0.kind == kind }
        if !items.isEmpty {
            Section(title) {
                ForEach(items) { item in
                    Button((model.isAdded(item) ? "✓ " : "    ") + item.label +
                           (item.url == nil ? "  (\(item.file) not beside it)" : "  — \(item.file)")) {
                        model.toggle(item)
                    }
                    .disabled(item.url == nil)
                }
            }
        }
    }
}

/// Info and, when the file has something to say, its health report.
struct InfoSheet: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.closePanel) private var dismiss
    @State private var showHealth = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(showHealth ? "File health" : "Info").font(.headline)
                Spacer()
                if model.healthLevel > 0 {
                    Button(showHealth ? "Info" : "File health") { showHealth.toggle() }
                }
                Button("Copy") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(text, forType: .string)
                }
                Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
            }
            ScrollView {
                Text(text)
                    .font(.system(.footnote, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    private var text: String { showHealth ? model.healthReport() : model.infoText() }
}

/// The core's settings schema, grouped by section; each choice is stored and applied at once.
struct SettingsView: View {
    @ObservedObject var model: ViewerModel

    private var sections: [String] {
        var seen: [String] = []
        for row in model.settingRows where !row.isSlot && !seen.contains(row.section) { seen.append(row.section) }
        return seen
    }

    var body: some View {
        Form {
            ForEach(sections, id: \.self) { section in
                Section(section) {
                    ForEach(model.settingRows.filter { $0.section == section && !$0.isSlot }) { row in
                        Picker(selection: Binding(
                            get: { closest(row) },
                            set: { model.set(row, to: row.values[$0]) })) {
                            ForEach(Array(row.choices.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
                        } label: {
                            Text(row.label)
                        }
                        .help(row.hint)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .frame(minWidth: 520, minHeight: 480)
    }

    /// The choice whose stored value is the current one (the core's dmc_settings_selected rule).
    private func closest(_ row: SettingRow) -> Int {
        let value = model.value(of: row)
        return row.values.indices.min { abs(row.values[$0] - value) < abs(row.values[$1] - value) } ?? 0
    }
}

/// A configuration marker's card: its rows, then its buttons (the core's).
/// The formats textures can be re-encoded to (the core's list), DX10 header optional.
struct TextureFormatSheet: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.closePanel) private var dismiss
    @State private var dx10 = false
    private let formats = ReaderSession.textureFormats

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Texture format").font(.headline)
            List(formats, id: \.name) { format in
                Button(format.label) {
                    dismiss()
                    model.reencodeTextures(format: format.name, dx10: dx10)
                }
                .buttonStyle(.plain)
            }
            .frame(minHeight: 220)
            Toggle("Always write the DX10 header", isOn: $dx10)
            HStack { Spacer(); Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction) }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

/// A composite model's parts: each hangs from a joint of the host, or sits where its file put it.
struct PartsSheet: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.closePanel) private var dismiss
    @State private var joints: [Int: Int] = [:]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Model parts").font(.headline)
            let host = model.partsShown.first
            List(model.partsShown) { part in
                VStack(alignment: .leading, spacing: 4) {
                    Text("\(part.id). \(part.name)").font(.subheadline.bold())
                    Text(part.state).font(.caption).foregroundStyle(.secondary)
                    if part.id > 0, let host {
                        HStack {
                            Picker("Joint of \(host.name)", selection: Binding(
                                get: { joints[part.id] ?? max(0, part.defaultAttachment) },
                                set: { joints[part.id] = $0 })) {
                                ForEach(Array(host.nodes.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
                            }
                            Button("Attach") {
                                model.attachPart(host: 0, child: part.id,
                                                 joint: joints[part.id] ?? max(0, part.defaultAttachment))
                            }
                            Button("Reset") { model.resetPart(part.id) }
                        }
                    }
                }
                .padding(.vertical, 2)
            }
            .frame(minHeight: 260)
            HStack { Spacer(); Button("Close") { dismiss() }.keyboardShortcut(.cancelAction) }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

/// Plain selectable text (diagnostics).
struct TextSheet: View {
    let title: String
    let text: String
    @Environment(\.closePanel) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            ScrollView {
                Text(text).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Button("Copy") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(text, forType: .string)
                }
                Spacer()
                Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

/// The core's collision report: each group's colour (as drawn), heading and lines.
struct CollisionReportSheet: View {
    let report: CollisionReport
    @Environment(\.closePanel) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(report.title).font(.headline)
            if !report.summary.isEmpty { Text(report.summary).foregroundStyle(.secondary) }
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    ForEach(report.groups) { group in
                        VStack(alignment: .leading, spacing: 3) {
                            HStack(spacing: 6) {
                                if group.rgb >= 0 {
                                    RoundedRectangle(cornerRadius: 2)
                                        .fill(Color(red: Double((group.rgb >> 16) & 0xFF) / 255,
                                                    green: Double((group.rgb >> 8) & 0xFF) / 255,
                                                    blue: Double(group.rgb & 0xFF) / 255))
                                        .frame(width: 12, height: 12)
                                }
                                Text(group.heading).font(.subheadline.bold())
                            }
                            Text(group.lines).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                        }
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Spacer()
                Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

struct MarkerCardSheet: View {
    @ObservedObject var model: ViewerModel
    let card: MarkerCardInfo
    @Environment(\.closePanel) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(card.title).font(.headline)
            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 4) {
                ForEach(card.rows, id: \.self) { row in
                    GridRow {
                        Text(row.name).foregroundStyle(.secondary)
                        Text(row.value).textSelection(.enabled)
                    }
                }
            }
            .font(.system(.callout, design: .monospaced))
            HStack {
                ForEach(card.buttons) { button in
                    Button(button.label) { model.runCard(button: button) }
                }
                Spacer()
                Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

/// Keys for fly mode: held keys go to the core as the two sticks (see ViewerModel.flyStep).
@MainActor
enum FlyKeys {
    private static var monitor: Any?

    static func install(model: ViewerModel) {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak model] event in
            guard let model else { return event }
            // The photo camera's keys: , . turn the focus ring, K locks focus, L levels.
            if !model.flying, model.inCameraMode, event.type == .keyDown,
               event.modifierFlags.intersection([.command, .control, .option]).isEmpty,
               !(event.window?.firstResponder is NSText),
               let c = event.charactersIgnoringModifiers?.lowercased() {
                switch c {
                case ",": model.focusRing(-1); return nil
                case ".": model.focusRing(1); return nil
                case "k": model.focusLock(); return nil
                case "l": model.levelCamera(); return nil
                default: break
                }
            }
            guard model.flying, let key = name(of: event) else { return event }
            model.flyKey(key, down: event.type == .keyDown)
            return nil  // the keys steer while flying
        }
    }

    private static func name(of event: NSEvent) -> String? {
        switch event.keyCode {
        case 123: return "left"
        case 124: return "right"
        case 125: return "down"
        case 126: return "up"
        default:
            guard let c = event.charactersIgnoringModifiers?.lowercased(), ["w", "a", "s", "d"].contains(c) else { return nil }
            return c
        }
    }
}

/// The photo camera's settings: the core's schema, each choice stored and handed to the core.
struct CameraSettingsView: View {
    @ObservedObject var model: ViewerModel

    private var sections: [String] {
        var seen: [String] = []
        for row in model.cameraRows where !seen.contains(row.section) { seen.append(row.section) }
        return seen
    }

    var body: some View {
        Form {
            ForEach(sections, id: \.self) { section in
                Section(section) {
                    ForEach(model.cameraRows.filter { $0.section == section }) { row in
                        Picker(selection: Binding(
                            get: { choice(of: row) },
                            set: { model.setCamera(row, option: $0) })) {
                            ForEach(Array(row.options.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
                        } label: { Text(row.label) }
                        .help(row.hint)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .frame(minWidth: 520, minHeight: 480)
    }

    private func choice(of row: CameraSettingRow) -> Int {
        guard let index = model.cameraRows.firstIndex(of: row), index < model.cameraChoices.count else { return row.start }
        return model.cameraChoices[index]
    }
}

/// About: the app's version and the commit its core was built from.
enum AboutPanel {
    static func show() {
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.1"
        let build = takeString(dmc_build_identity()) ?? "unknown"
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationVersion: version,
            .version: "core \(build), C API \(dmc_api_version())",
            .credits: NSAttributedString(string: "A read-only viewer of the game's files: the core decides, this shell shows."),
        ])
    }
}

/// Closes the side panel a former sheet sits in (the panel's owner sets it).
struct ClosePanelAction {
    let run: () -> Void
    init(_ run: @escaping () -> Void = {}) { self.run = run }
    func callAsFunction() { run() }
}

private struct ClosePanelKey: EnvironmentKey {
    static let defaultValue = ClosePanelAction()
}

extension EnvironmentValues {
    var closePanel: ClosePanelAction {
        get { self[ClosePanelKey.self] }
        set { self[ClosePanelKey.self] = newValue }
    }
}

/// The opened archive's in-game looks. Names made of the same number of
/// " · " parts (Vergil: look · weapons · DT) get one switch per part;
/// otherwise one menu lists them all.
struct VariantBar: View {
    @ObservedObject var model: ViewerModel

    private var parts: [[String]] { model.variants.map { $0.components(separatedBy: " · ") } }

    private var columns: Int? {
        let counts = Set(parts.map(\.count))
        guard counts.count == 1, let n = counts.first, n > 1 else { return nil }
        return n
    }

    var body: some View {
        HStack(spacing: 12) {
            if let n = columns {
                ForEach(0..<n, id: \.self) { column in
                    Picker("", selection: Binding(
                        get: { current(column) },
                        set: { choose(column: column, value: $0) })) {
                        ForEach(options(column), id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden()
                    .fixedSize()
                }
            } else {
                Picker("Look", selection: Binding(
                    get: { model.variantIndex },
                    set: { model.selectVariant($0) })) {
                    ForEach(Array(model.variants.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
                }
                .fixedSize()
            }
            Spacer()
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private func options(_ column: Int) -> [String] {
        var seen: [String] = []
        for p in parts where !seen.contains(p[column]) { seen.append(p[column]) }
        return seen
    }

    private func current(_ column: Int) -> String {
        guard parts.indices.contains(model.variantIndex) else { return "" }
        return parts[model.variantIndex][column]
    }

    /// The look with this part changed and the others kept; else the first with it.
    private func choose(column: Int, value: String) {
        guard parts.indices.contains(model.variantIndex) else { return }
        var wanted = parts[model.variantIndex]
        wanted[column] = value
        if let exact = parts.firstIndex(of: wanted) {
            model.selectVariant(exact)
        } else if let any = parts.firstIndex(where: { $0[column] == value }) {
            model.selectVariant(any)
        }
    }
}
