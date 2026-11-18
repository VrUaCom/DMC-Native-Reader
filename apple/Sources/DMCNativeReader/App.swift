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

    private var viewer: some View {
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
