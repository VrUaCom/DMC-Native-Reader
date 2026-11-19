// A full stage is four archives (stNNN.pac, stNNN_effect.pac, stNNNcfg.pac,
// its sounds). When a base room loads, the core says which of the previous
// stage's effect bank and configuration stay (dmc_after_new_room); the shell
// then looks beside the file for the stage's own (dmc_stage_archives), as
// Android looks in its game folder.
import CDMCReader
import Foundation

extension ViewerModel {
    /// After a base room loaded from `url`: drop the old stage's bank and
    /// configuration unless they belong to it, then load its own from beside it.
    func afterNewRoom(url: URL) {
        let room = url.lastPathComponent
        var change = dmc_after_new_room(room, baseEffectsName ?? "", baseConfigName ?? "")
        defer { dmc_room_change_free(&change) }
        if !change.keep_effects {
            dmc_room_effects_clear(0)
            baseEffectsName = nil
        }
        if !change.keep_config {
            dmc_stage_config_clear(0)
            baseConfigName = nil
        }
        findStageArchives(near: url, stage: room, slot: 0, effects: !change.keep_effects, config: !change.keep_config)
    }

    /// The stage's effect bank and configuration from `url`'s folder onto `slot`.
    func findStageArchives(near url: URL, stage: String, slot: Int, effects: Bool, config: Bool) {
        guard effects || config else { return }
        var effectsName: UnsafeMutablePointer<CChar>?
        var configName: UnsafeMutablePointer<CChar>?
        guard dmc_stage_archives(stage, &effectsName, &configName) else { return }
        let wantedEffects = takeString(effectsName) ?? ""
        let wantedConfig = takeString(configName) ?? ""
        let folder = url.deletingLastPathComponent()
        let files = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        func find(_ wanted: String) -> URL? {
            guard !wanted.isEmpty else { return nil }
            return files.first { $0.caseInsensitiveCompare(wanted) == .orderedSame }.map { folder.appendingPathComponent($0) }
        }
        var found: [String] = []
        if effects, let file = find(wantedEffects), let data = try? Data(contentsOf: file, options: .mappedIfSafe) {
            let detail = data.withUnsafeBytes { raw in
                takeString(dmc_room_effects_load(file.lastPathComponent, raw.bindMemory(to: UInt8.self).baseAddress,
                                                 raw.count, Int32(slot)))
            }
            if detail != nil {
                found.append(file.lastPathComponent)
                if slot == 0 { baseEffectsName = file.lastPathComponent }
            }
        }
        if config, let file = find(wantedConfig), let data = try? Data(contentsOf: file, options: .mappedIfSafe) {
            let outcome = ReaderCore.loadStageConfig(name: file.lastPathComponent, bytes: data, slot: slot)
            if !outcome.message.isEmpty { found.append(file.lastPathComponent) }
            if slot == 0 { baseConfigName = file.lastPathComponent }
        }
        if found.isEmpty {
            let names = [wantedEffects, wantedConfig].filter { !$0.isEmpty }.joined(separator: ", ")
            if !names.isEmpty { message = (message.map { $0 + "\n" } ?? "") + "Not beside it: \(names)" }
        } else {
            message = (message.map { $0 + "\n" } ?? "") + "Loaded with it: " + found.joined(separator: ", ")
        }
        refreshAfterChange()
    }
}
