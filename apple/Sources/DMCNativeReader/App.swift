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
                .onAppear { appDelegate.model = model }
                .onOpenURL { model.open(url: $0) }
        }
        Settings {
            SettingsView(model: model)
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open…") { OpenPanel.choose { model.open(url: $0) } }
                    .keyboardShortcut("o")
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

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Started from Terminal (swift run) the app would stay in the background.
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
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
    @State private var dragLast: CGSize?
    @State private var zoomStart: Float?
    @State private var twistLast: Double?
    @State private var spinTimer: Timer?

    var body: some View {
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
            if let message = model.message {
                VStack {
                    Spacer()
                    Text(message)
                        .font(.callout)
                        .padding(10)
                        .background(.black.opacity(0.75), in: RoundedRectangle(cornerRadius: 8))
                        .padding()
                }
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
        .sheet(isPresented: $showInfo) { InfoSheet(model: model) }
        .sheet(item: $model.markerCard) { card in MarkerCardSheet(model: model, card: card) }
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

    private var viewport: some View {
        GeometryReader { geometry in
            ZStack {
                if let image = model.image {
                    Image(decorative: image, scale: 1)
                        .resizable()
                        .interpolation(model.isPicture ? .none : .high)
                        .aspectRatio(contentMode: .fit)
                } else if !model.drawsNothing {
                    ProgressView()
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .contentShape(Rectangle())
            .gesture(orbit)
            .simultaneousGesture(LongPressGesture(minimumDuration: 0.5).sequenced(before: DragGesture(minimumDistance: 0))
                .onEnded { value in
                    if case .second(true, let drag?) = value { model.hold(at: drag.location) }
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
                let last = dragLast ?? .zero
                dragLast = value.translation
                model.camera.drag(dx: Float(value.translation.width - last.width),
                                  dy: Float(value.translation.height - last.height))
            }
            .onEnded { value in
                dragLast = nil
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
                let start = zoomStart ?? model.camera.zoom
                if zoomStart == nil { zoomStart = start }
                model.camera.pinch(startZoom: start, scale: Float(scale))
            }
            .onEnded { _ in zoomStart = nil }
    }

    /// Two fingers turning on the trackpad turn the view.
    private var twist: some Gesture {
        RotationGesture()
            .onChanged { angle in
                let last = twistLast ?? 0
                twistLast = angle.radians
                model.camera.twist(radians: dmc_angle_delta(Float(last), Float(angle.radians)), roomShown: false)
            }
            .onEnded { _ in twistLast = nil }
    }

    @ToolbarContentBuilder
    private var toolbar: some ToolbarContent {
        ToolbarItemGroup(placement: .navigation) {
            Button { model.goBack() } label: { Label("Back", systemImage: "chevron.backward") }
                .disabled(!model.canGoBack)
        }
        ToolbarItemGroup {
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
                    Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.run(action: row) }
                }
                if !model.moreActions.isEmpty {
                    Menu("Other actions") {
                        ForEach(model.moreActions) { row in
                            Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.run(action: row) }
                        }
                    }
                }
            } label: { Label("Actions", systemImage: "ellipsis.circle") }
                .disabled(model.actions.isEmpty && model.moreActions.isEmpty)
            if !model.effects.isEmpty {
                Menu {
                    ForEach(model.effects) { row in
                        Button(row.icon.isEmpty ? row.label : "\(row.icon) \(row.label)") { model.playEffect(row) }
                    }
                } label: { Label("Effects", systemImage: "sparkles") }
            }
            Menu {
                Button("Load base room…") { OpenPanel.choose { model.loadRoom(url: $0) } }
                Button("Clear base room") { model.clearRoom() }
                    .disabled(model.roomName == nil)
                Divider()
                Button("Load stage configuration…") { OpenPanel.choose { model.loadStageConfig(url: $0) } }
                    .disabled(model.session == nil)
            } label: { Label(model.roomName ?? "Room", systemImage: "cube.transparent") }
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

/// Info and, when the file has something to say, its health report.
struct InfoSheet: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.dismiss) private var dismiss
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
        .frame(minWidth: 560, idealWidth: 720, minHeight: 420, idealHeight: 620)
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
                            ForEach(row.choices.indices, id: \.self) { Text(row.choices[$0]).tag($0) }
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
struct MarkerCardSheet: View {
    @ObservedObject var model: ViewerModel
    let card: MarkerCardInfo
    @Environment(\.dismiss) private var dismiss

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
        .frame(minWidth: 420)
    }
}
