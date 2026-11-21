// The settings beside the view, as on Android: every row the core lists
// (graphics, lines and overlays, animation, base room) with its hint under
// it, the shell's own pieces (benchmark, the base room's file), and the
// photo camera's settings. The core writes the rows and says what the stored
// values mean; the shell only draws and stores them.

import AppKit
import SwiftUI

struct SettingsPanel: View {
    @ObservedObject var model: ViewerModel
    @Environment(\.closePanel) private var close
    @State private var tab = 0

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Settings").font(.headline)
                Spacer()
                Button { close() } label: { Image(systemName: "xmark") }
                    .buttonStyle(.borderless)
                    .keyboardShortcut(.cancelAction)
                    .help("Close the settings")
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            Picker("", selection: $tab) {
                Text("View").tag(0)
                Text("Camera").tag(1)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .padding(.horizontal, 12)
            .padding(.bottom, 6)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    if tab == 0 { viewRows } else { cameraRows }
                }
                .padding(12)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .frame(minWidth: 300, maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    }

    // MARK: View

    private var sections: [String] {
        var seen: [String] = []
        for row in model.settingRows where !seen.contains(row.section) { seen.append(row.section) }
        if !seen.contains("Base room") { seen.append("Base room") }
        return seen
    }

    @ViewBuilder private var viewRows: some View {
        ForEach(sections, id: \.self) { section in
            VStack(alignment: .leading, spacing: 10) {
                Text(section).font(.subheadline.weight(.semibold)).foregroundStyle(.secondary)
                ForEach(model.settingRows.filter { $0.section == section }) { row in
                    if row.isSlot {
                        slot(row)
                    } else if !row.roomOnly || model.roomName != nil {
                        settingRow(row)
                    }
                }
                if section == "Base room" { baseRoom }
            }
            Divider()
        }
    }

    private func settingRow(_ row: SettingRow) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(row.label)
                .fixedSize(horizontal: false, vertical: true)
            Picker("", selection: Binding(
                get: { closest(row) },
                set: { model.set(row, to: row.values[$0]) })) {
                ForEach(Array(row.choices.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
            }
            .labelsHidden()
            .frame(maxWidth: .infinity, alignment: .leading)
            if !row.hint.isEmpty {
                Text(row.hint)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
            }
        }
    }

    @ViewBuilder private func slot(_ row: SettingRow) -> some View {
        switch row.key {
        case "slot.benchmark":
            VStack(alignment: .leading, spacing: 4) {
                Button("Run the benchmark (10 s)") { model.runBenchmark() }
                    .disabled(model.session == nil || model.isPicture)
                Text("Draws the open file for ten seconds with these settings and reports the frame times.")
                    .font(.caption).foregroundStyle(.secondary)
            }
        case "slot.collisionKinds":
            Text("Which collision kinds are drawn: toolbar → Collision.")
                .font(.caption).foregroundStyle(.secondary)
        default:
            EmptyView()
        }
    }

    @ViewBuilder private var baseRoom: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(model.roomName.map { "Now: \($0)" } ?? "No base room: the model stands on a plain floor.")
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack(spacing: 6) {
                Button("Choose a room…") { OpenPanel.choose { model.loadRoom(url: $0) } }
                Button("Next spot") { model.nextSpot() }
                    .disabled(model.roomName == nil)
                Button("Remove") { model.clearRoom() }
                    .disabled(model.roomName == nil)
            }
        }
    }

    /// The choice whose stored value is the current one (the core's dmc_settings_selected rule).
    private func closest(_ row: SettingRow) -> Int {
        let value = model.value(of: row)
        return row.values.indices.min { abs(row.values[$0] - value) < abs(row.values[$1] - value) } ?? 0
    }

    // MARK: Camera

    private var cameraSections: [String] {
        var seen: [String] = []
        for row in model.cameraRows where !seen.contains(row.section) { seen.append(row.section) }
        return seen
    }

    @ViewBuilder private var cameraRows: some View {
        ForEach(cameraSections, id: \.self) { section in
            VStack(alignment: .leading, spacing: 10) {
                Text(section).font(.subheadline.weight(.semibold)).foregroundStyle(.secondary)
                ForEach(model.cameraRows.filter { $0.section == section }) { row in
                    VStack(alignment: .leading, spacing: 4) {
                        Text(row.label)
                            .fixedSize(horizontal: false, vertical: true)
                        Picker("", selection: Binding(
                            get: { choice(of: row) },
                            set: { model.setCamera(row, option: $0) })) {
                            ForEach(Array(row.options.enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
                        }
                        .labelsHidden()
                        .frame(maxWidth: .infinity, alignment: .leading)
                        if !row.hint.isEmpty {
                            Text(row.hint)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
            }
            Divider()
        }
    }

    private func choice(of row: CameraSettingRow) -> Int {
        guard let index = model.cameraRows.firstIndex(of: row), index < model.cameraChoices.count else { return row.start }
        return model.cameraChoices[index]
    }
}
