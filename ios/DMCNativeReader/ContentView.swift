import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var store: ResourceStore
    @State private var showingImporter = false
    @State private var showingInspector = false

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
        .sheet(isPresented: $showingInspector) {
            InspectorView(text: store.resource?.inspectionText ?? "No inspection data.")
        }
        .alert("Open failed",
               isPresented: Binding(
                get: { store.errorMessage != nil },
                set: { if !$0 { store.errorMessage = nil } }
               )) {
            Button("OK", role: .cancel) { store.errorMessage = nil }
        } message: {
            Text(store.errorMessage ?? "Unknown error")
        }
    }

    private var statusBar: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("DMC Native Reader")
                    .font(.system(size: 15, weight: .bold))
                Spacer()
                Text("iOS · v1.0")
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(Color(white: 0.62))
            }
            if !store.fileName.isEmpty {
                Text(store.fileName)
                    .font(.system(size: 13, weight: .semibold))
            }
            Text(store.status)
                .font(.system(size: 10.5, design: .monospaced))
                .foregroundStyle(Color(white: 0.76))
                .textSelection(.enabled)
        }
        .foregroundStyle(.white)
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }

    @ViewBuilder
    private var content: some View {
        if let resource = store.resource {
            if resource.hasGeometry {
                MeshView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if let image = resource.imagePreview {
                Image(uiImage: image)
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
                    .padding(12)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if resource.childCount > 0 {
                ChildGallery(resource: resource)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                VStack(spacing: 10) {
                    Image(systemName: "doc.text.magnifyingglass")
                        .font(.system(size: 38))
                    Text("Resource decoded")
                        .font(.headline)
                    Text("Open Inspector for typed Architecture v2 data.")
                        .font(.footnote)
                        .foregroundStyle(Color(white: 0.65))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        } else {
            VStack(spacing: 10) {
                Image(systemName: "doc.viewfinder")
                    .font(.system(size: 42))
                Text("Open a DMC resource")
                    .font(.headline)
                Text("MOD · SCM · DDS · PTX")
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundStyle(Color(white: 0.62))
                Text("The same C++20 Architecture v2 core is shared with Android.")
                    .font(.footnote)
                    .multilineTextAlignment(.center)
                    .foregroundStyle(Color(white: 0.55))
                    .padding(.horizontal, 28)
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var controls: some View {
        HStack(spacing: 10) {
            Button("Open") { showingImporter = true }
            Button("Inspector") { showingInspector = true }
                .disabled(!store.hasResource)
            Button("Reset") { store.resetView() }
                .disabled(!store.hasGeometry)
            Button(store.wireframe ? "Wire: on" : "Wire: off") {
                store.toggleWireframe()
            }
            .disabled(!store.hasGeometry)
        }
        .buttonStyle(.bordered)
        .tint(.white)
        .padding(.horizontal, 10)
        .padding(.vertical, 10)
        .frame(maxWidth: .infinity)
    }
}

private struct InspectorView: View {
    let text: String

    var body: some View {
        NavigationStack {
            ScrollView([.vertical, .horizontal]) {
                Text(text)
                    .font(.system(size: 11, design: .monospaced))
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(16)
            }
            .navigationTitle("Inspector")
            .navigationBarTitleDisplayMode(.inline)
        }
        .preferredColorScheme(.dark)
    }
}

private struct ChildGallery: View {
    let resource: DmcResource
    @State private var selected = 0

    private var count: Int { Int(resource.childCount) }

    var body: some View {
        VStack(spacing: 10) {
            if selected >= 0,
               selected < count,
               let image = resource.childImage(at: UInt(selected)) {
                Image(uiImage: image)
                    .resizable()
                    .interpolation(.none)
                    .scaledToFit()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .padding(.horizontal, 12)
            }

            ScrollView(.horizontal, showsIndicators: true) {
                HStack(spacing: 8) {
                    ForEach(0..<count, id: \.self) { index in
                        Button {
                            selected = index
                        } label: {
                            VStack(spacing: 4) {
                                if let image = resource.childImage(at: UInt(index)) {
                                    Image(uiImage: image)
                                        .resizable()
                                        .interpolation(.none)
                                        .scaledToFit()
                                        .frame(width: 88, height: 88)
                                        .background(Color.black)
                                } else {
                                    Rectangle()
                                        .fill(Color(white: 0.12))
                                        .frame(width: 88, height: 88)
                                        .overlay(Image(systemName: "photo"))
                                }
                                Text(resource.childTitle(at: UInt(index)) ?? "Child \(index)")
                                    .font(.system(size: 9, design: .monospaced))
                                    .lineLimit(1)
                                    .frame(width: 96)
                            }
                            .padding(5)
                            .background(selected == index ? Color.white.opacity(0.18) : Color.clear)
                            .clipShape(RoundedRectangle(cornerRadius: 8))
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, 12)
            }
            .frame(height: 125)
        }
    }
}

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
