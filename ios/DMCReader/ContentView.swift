import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var store: ResourceStore
    @State private var showingImporter = false

    var body: some View {
        VStack(spacing: 0) {
            statusBar
            content
            controls
        }
        .background(Color(red: 0.043, green: 0.043, blue: 0.055))
        .fileImporter(
            isPresented: $showingImporter,
            allowedContentTypes: DmcContentType.pickable,
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { store.open(url: url) }
            case .failure(let error):
                store.errorMessage = error.localizedDescription
            }
        }
    }

    private var statusBar: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !store.fileName.isEmpty {
                Text(store.fileName)
                    .font(.system(size: 14, weight: .semibold))
                    .foregroundStyle(.white)
            }
            Text(store.status)
                .font(.system(size: 11, design: .monospaced))
                .foregroundStyle(Color(white: 0.78))
                .textSelection(.enabled)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    @ViewBuilder
    private var content: some View {
        if let resource = store.resource, let text = resource.text {
            // Text families (stage .txt, .index) have no geometry to show.
            ScrollView([.vertical, .horizontal]) {
                Text(text)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Color(white: 0.84))
                    .textSelection(.enabled)
                    .padding(16)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else if store.resource != nil {
            MeshView()
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            VStack(spacing: 8) {
                Text("Open a DMC resource")
                    .font(.headline)
                    .foregroundStyle(.white)
                Text("SCM · MOD · HITS · stage .txt · .index")
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundStyle(Color(white: 0.6))
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var controls: some View {
        HStack(spacing: 12) {
            Button("Open") { showingImporter = true }
            Button("Reset") { store.resetView() }
                .disabled(!store.hasResource || store.isText)
            Button(store.wireframe ? "Wire: on" : "Wire: off") {
                store.toggleWireframe()
            }
            .disabled(!store.hasResource || store.isText)
        }
        .buttonStyle(.bordered)
        .tint(.white)
        .padding(.vertical, 10)
        .frame(maxWidth: .infinity)
    }
}

/// Renders the decoded mesh with the shared CPU renderer and maps drag/pinch
/// onto the same yaw/pitch/zoom state the Android view uses.
private struct MeshView: View {
    @EnvironmentObject private var store: ResourceStore
    @State private var dragStart: (yaw: Float, pitch: Float)?
    @State private var zoomStart: Float?

    var body: some View {
        GeometryReader { geometry in
            let scale = UIScreen.main.scale
            let pixelSize = CGSize(width: geometry.size.width * scale,
                                   height: geometry.size.height * scale)
            Group {
                if let image = store.resource?.render(
                    with: pixelSize,
                    yaw: store.yaw,
                    pitch: store.pitch,
                    zoom: store.zoom,
                    wireframe: store.wireframe) {
                    Image(uiImage: image)
                        .resizable()
                        .interpolation(.none)
                        .scaledToFill()
                } else {
                    Color.black
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipped()
            .contentShape(Rectangle())
            .gesture(
                DragGesture()
                    .onChanged { value in
                        let start = dragStart ?? (store.yaw, store.pitch)
                        if dragStart == nil { dragStart = start }
                        store.yaw = start.yaw + Float(value.translation.width) * 0.01
                        store.pitch = start.pitch + Float(value.translation.height) * 0.01
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .simultaneousGesture(
                MagnificationGesture()
                    .onChanged { value in
                        let start = zoomStart ?? store.zoom
                        if zoomStart == nil { zoomStart = start }
                        store.zoom = start * Float(value)
                    }
                    .onEnded { _ in zoomStart = nil }
            )
        }
    }
}
