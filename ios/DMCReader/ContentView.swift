import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var store: ResourceStore
    @State private var pickingResource = false
    @State private var pickingTexture = false
    @State private var showingInfo = false

    var body: some View {
        VStack(spacing: 0) {
            header
            content
            toolbar
        }
        .background(Color(red: 0.043, green: 0.043, blue: 0.055).ignoresSafeArea())
        .fileImporter(isPresented: $pickingResource,
                      allowedContentTypes: DmcContentType.resources) { result in
            if case .success(let url) = result { store.open(url: url) }
        }
        .background(
            EmptyView().fileImporter(isPresented: $pickingTexture,
                                     allowedContentTypes: DmcContentType.textures) { result in
                if case .success(let url) = result { store.attachTexture(url: url) }
            })
        .sheet(isPresented: $showingInfo) { InfoSheet() }
        .alert("Could not open", isPresented: errorBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(store.errorMessage ?? "")
        }
        .alert("Texture", isPresented: noticeBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(store.notice ?? "")
        }
    }

    private var errorBinding: Binding<Bool> {
        Binding(get: { store.errorMessage != nil }, set: { if !$0 { store.errorMessage = nil } })
    }

    private var noticeBinding: Binding<Bool> {
        Binding(get: { store.notice != nil }, set: { if !$0 { store.notice = nil } })
    }

    private var header: some View {
        HStack(spacing: 12) {
            if store.canGoBack {
                Button { store.goBack() } label: { Image(systemName: "chevron.backward") }
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(store.current?.title ?? "DMC Native Reader")
                    .font(.system(size: 15, weight: .semibold))
                    .lineLimit(1)
                if store.stack.count > 1 {
                    Text(store.stack.map(\.title).joined(separator: " › "))
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
            }
            Spacer()
            if store.current != nil {
                Button { showingInfo = true } label: { Image(systemName: "info.circle") }
            }
        }
        .foregroundStyle(.white)
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    @ViewBuilder
    private var content: some View {
        // Presentation follows Black Widow state, in the same priority order
        // as the Android shell: child browser, then model, then image.
        let _ = store.revision
        if let resource = store.current {
            if resource.isChildBrowser {
                ChildBrowser(resource: resource)
            } else if resource.canRender {
                MeshView(resource: resource)
            } else if resource.canPreviewImage, let image = resource.imagePreview() {
                ZoomableImage(image: image)
            } else {
                ScrollView {
                    Text(resource.inspection)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(Color(white: 0.84))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(16)
                }
            }
        } else {
            VStack(spacing: 8) {
                Image(systemName: "cube.transparent")
                    .font(.system(size: 40))
                    .foregroundStyle(Color(white: 0.5))
                Text("Open a DMC resource")
                    .font(.headline)
                    .foregroundStyle(.white)
                Text("MOD · SCM · DDS · PTX · EventTbl · TM2")
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundStyle(Color(white: 0.6))
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private var toolbar: some View {
        let _ = store.revision
        let resource = store.current
        let isModel = resource?.canRender == true && resource?.isChildBrowser == false
        return HStack(spacing: 10) {
            Button("Open") { pickingResource = true }
            if isModel {
                Button("Reset") { store.resetView() }
                if resource?.canWireframe == true {
                    Button(store.wireframe ? "Wire: on" : "Wire: off") {
                        store.wireframe.toggle()
                    }
                }
            }
            if resource?.canAttachTexture == true {
                Button(resource?.textureAttached == true ? "Texture ✓" : "Texture") {
                    pickingTexture = true
                }
            }
        }
        .buttonStyle(.bordered)
        .tint(.white)
        .padding(.vertical, 10)
        .frame(maxWidth: .infinity)
    }
}

/// CPU render of the current model. Drag rotates, pinch zooms — the same
/// yaw/pitch/zoom state the Android and Windows shells feed the Core renderer.
private struct MeshView: View {
    @EnvironmentObject private var store: ResourceStore
    @Environment(\.displayScale) private var displayScale
    let resource: DmcResource
    @State private var dragStart: (yaw: Float, pitch: Float)?
    @State private var zoomStart: Float?

    var body: some View {
        GeometryReader { geometry in
            let pixels = CGSize(width: geometry.size.width * displayScale,
                                height: geometry.size.height * displayScale)
            let _ = store.revision
            Group {
                if let image = resource.render(with: pixels, yaw: store.yaw, pitch: store.pitch,
                                               zoom: store.zoom, wireframe: store.wireframe) {
                    Image(uiImage: image)
                        .resizable()
                        .interpolation(.medium)
                        .scaledToFit()
                } else {
                    Color.black
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
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
                        store.zoom = min(max(start * Float(value), 0.15), 8.0)
                    }
                    .onEnded { _ in zoomStart = nil }
            )
        }
    }
}

/// Decoded texture (DDS/TM2) with pinch-to-zoom.
private struct ZoomableImage: View {
    let image: UIImage
    @State private var scale: CGFloat = 1
    @State private var base: CGFloat = 1

    var body: some View {
        Image(uiImage: image)
            .resizable()
            .interpolation(.none)
            .scaledToFit()
            .scaleEffect(scale)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(CheckerBackground())
            .gesture(
                MagnificationGesture()
                    .onChanged { scale = min(max(base * $0, 0.5), 16) }
                    .onEnded { _ in base = scale }
            )
            .onTapGesture(count: 2) {
                scale = 1
                base = 1
            }
    }
}

/// Makes texture alpha visible.
private struct CheckerBackground: View {
    var body: some View {
        Canvas { context, size in
            let cell: CGFloat = 12
            for row in 0..<Int(size.height / cell) + 1 {
                for column in 0..<Int(size.width / cell) + 1 where (row + column) % 2 == 0 {
                    context.fill(Path(CGRect(x: CGFloat(column) * cell, y: CGFloat(row) * cell,
                                             width: cell, height: cell)),
                                 with: .color(Color(white: 0.16)))
                }
            }
        }
        .background(Color(white: 0.10))
    }
}

/// Grid of the typed children a container publishes (e.g. textures in a PTX).
private struct ChildBrowser: View {
    @EnvironmentObject private var store: ResourceStore
    let resource: DmcResource

    var body: some View {
        ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 110), spacing: 12)], spacing: 12) {
                ForEach(0..<resource.childCount, id: \.self) { index in
                    Button { store.openChild(at: index) } label: {
                        VStack(spacing: 6) {
                            Group {
                                if let thumb = resource.childPreview(at: index) {
                                    Image(uiImage: thumb).resizable().interpolation(.none).scaledToFit()
                                } else {
                                    Image(systemName: "doc").font(.system(size: 28))
                                }
                            }
                            .frame(width: 100, height: 100)
                            .background(CheckerBackground())
                            .clipShape(RoundedRectangle(cornerRadius: 6))
                            Text(resource.childTitle(at: index))
                                .font(.system(size: 11))
                                .lineLimit(2)
                                .foregroundStyle(.white)
                        }
                    }
                }
            }
            .padding(16)
        }
    }
}

private struct InfoSheet: View {
    @EnvironmentObject private var store: ResourceStore
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    if let resource = store.current {
                        section("Session", resource.summary)
                        section("Structure", resource.inspection)
                        if resource.canAttachTexture {
                            section("Texture companion", resource.textureAttachmentDetail)
                        }
                    }
                }
                .padding(16)
            }
            .navigationTitle(store.current?.title ?? "")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
            }
        }
    }

    private func section(_ title: String, _ body: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.headline)
            Text(body.isEmpty ? "—" : body)
                .font(.system(size: 12, design: .monospaced))
                .textSelection(.enabled)
        }
    }
}
