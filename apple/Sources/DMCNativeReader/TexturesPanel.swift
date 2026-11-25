// The model's textures beside the view, and pictures of your own over them:
// drop a PNG or DDS on a row (or choose one) to see a modded costume on the
// character without repacking anything. The core decodes the file and swaps
// the texture it draws with; nothing is written. Export gives the original
// as a PNG to edit; Restore brings it back.

import AppKit
import CDMCReader
import SwiftUI
import UniformTypeIdentifiers

struct TextureSlot: Identifiable, Equatable {
    let id: Int
    let width: Int
    let height: Int
    let overrideName: String?
    let thumbnail: CGImage?

    static func == (a: TextureSlot, b: TextureSlot) -> Bool {
        a.id == b.id && a.width == b.width && a.height == b.height && a.overrideName == b.overrideName
    }
}

extension ReaderSession {
    func textureSlots(thumbnail side: Int = 96) -> [TextureSlot] {
        let count = dmc_texture_count(handle)
        return (0..<count).map { slot in
            var width: Int32 = 0
            var height: Int32 = 0
            var overridden = false
            var name: UnsafeMutablePointer<CChar>?
            _ = dmc_texture_info(handle, slot, &width, &height, &overridden, &name)
            let overrideName = takeString(name)
            return TextureSlot(id: Int(slot), width: Int(width), height: Int(height),
                               overrideName: overridden ? (overrideName ?? "picture") : nil,
                               thumbnail: textureImage(slot: slot, maxSide: side, original: false))
        }
    }

    func textureImage(slot: Int, maxSide: Int, original: Bool) -> CGImage? {
        guard let image = dmc_texture_image(handle, slot, Int32(maxSide), original) else { return nil }
        defer { dmc_image_free(image) }
        return makeCGImage(RGBAImage(copying: image.pointee))
    }

    /// False with the core's reason.
    func overrideTexture(slot: Int, name: String, bytes: Data) -> (Bool, String) {
        var message: UnsafeMutablePointer<CChar>?
        let ok = bytes.withUnsafeBytes { raw in
            dmc_texture_override(handle, slot, name, raw.bindMemory(to: UInt8.self).baseAddress, raw.count, &message)
        }
        return (ok, takeString(message) ?? (ok ? "" : "The file could not be shown."))
    }

    func restoreTexture(slot: Int?) -> Bool {
        dmc_texture_restore(handle, slot.map { $0 } ?? Int.max)
    }
}

func makeCGImage(_ image: RGBAImage) -> CGImage? {
    let data = Data(image.pixels) as CFData
    guard let provider = CGDataProvider(data: data) else { return nil }
    return CGImage(width: image.width, height: image.height, bitsPerComponent: 8, bitsPerPixel: 32,
                   bytesPerRow: image.width * 4, space: CGColorSpaceCreateDeviceRGB(),
                   bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                   provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
}

extension ViewerModel {
    func refreshTextureSlots() {
        textureSlots = session?.textureSlots() ?? []
    }

    func overrideTexture(slot: Int, url: URL) {
        guard let session else { return }
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else {
            message = "Could not read \(url.lastPathComponent)."
            return
        }
        let (ok, text) = session.overrideTexture(slot: slot, name: url.lastPathComponent, bytes: data)
        message = text
        if ok {
            refreshTextureSlots()
            requestFrame()
        }
    }

    func restoreTexture(slot: Int?) {
        guard let session else { return }
        if session.restoreTexture(slot: slot) {
            message = slot.map { "Texture #\($0): the original again." } ?? "Every texture: the originals again."
            refreshTextureSlots()
            requestFrame()
        }
    }

    func exportTexture(slot: Int, original: Bool) {
        guard let session, let image = session.textureImage(slot: slot, maxSide: 0, original: original) else { return }
        SavePanel.choose(name: "texture_\(slot).png", type: .png) { [weak self] url in
            guard let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else {
                self?.message = "Could not write \(url.lastPathComponent)."
                return
            }
            CGImageDestinationAddImage(destination, image, nil)
            self?.message = CGImageDestinationFinalize(destination)
                ? "Texture #\(slot) saved as \(url.lastPathComponent): edit it and drop it back on its row."
                : "Could not write \(url.lastPathComponent)."
        }
    }
}

struct TexturesPanel: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.closePanel) private var close
    @State private var targeted: Int?

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Textures").font(.headline)
                Spacer()
                if model.textureSlots.contains(where: { $0.overrideName != nil }) {
                    Button("Restore all") { model.restoreTexture(slot: nil) }
                }
                Button { close() } label: { Image(systemName: "xmark") }
                    .buttonStyle(.borderless)
                    .help("Close")
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            Text("Drop a PNG or DDS on a texture to see it on the model. Nothing is written: Restore brings the "
                 + "original back. Export saves the original as a PNG to edit.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.horizontal, 12)
                .padding(.bottom, 6)
            Divider()
            if model.textureSlots.isEmpty {
                Text("This file draws no textures.")
                    .foregroundStyle(.secondary)
                    .padding()
                Spacer()
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 8) {
                        ForEach(model.textureSlots) { slot in row(slot) }
                    }
                    .padding(10)
                }
            }
        }
        .frame(minWidth: 300, maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        .onAppear { model.refreshTextureSlots() }
    }

    private func row(_ slot: TextureSlot) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Group {
                if let thumb = slot.thumbnail {
                    Image(decorative: thumb, scale: 1).resizable().interpolation(.medium).aspectRatio(contentMode: .fit)
                } else {
                    Color.gray.opacity(0.2)
                }
            }
            .frame(width: 72, height: 72)
            .background(Color.black.opacity(0.25))
            .clipShape(RoundedRectangle(cornerRadius: 4))
            VStack(alignment: .leading, spacing: 4) {
                Text("#\(slot.id) · \(slot.width) × \(slot.height)").font(.system(.body, design: .monospaced))
                if let name = slot.overrideName {
                    Text("Shows \(name)").font(.caption).foregroundStyle(.orange).lineLimit(2)
                }
                HStack(spacing: 6) {
                    Button("Replace…") {
                        let panel = NSOpenPanel()
                        panel.allowedContentTypes = [.png, UTType(filenameExtension: "dds") ?? .data]
                        panel.allowsOtherFileTypes = true
                        if panel.runModal() == .OK, let url = panel.url { model.overrideTexture(slot: slot.id, url: url) }
                    }
                    Button("Export") { model.exportTexture(slot: slot.id, original: true) }
                    if slot.overrideName != nil {
                        Button("Restore") { model.restoreTexture(slot: slot.id) }
                    }
                }
                .controlSize(.small)
            }
            Spacer(minLength: 0)
        }
        .padding(6)
        .background(RoundedRectangle(cornerRadius: 6)
            .fill(targeted == slot.id ? Color.accentColor.opacity(0.25) : Color.clear))
        .onDrop(of: [.fileURL], isTargeted: Binding(get: { targeted == slot.id },
                                                     set: { targeted = $0 ? slot.id : nil })) { providers in
            guard let provider = providers.first else { return false }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                if let url { Task { @MainActor in model.overrideTexture(slot: slot.id, url: url) } }
            }
            return true
        }
    }
}
