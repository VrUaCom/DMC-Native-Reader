#include "dmcresource/pac_assembly.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "dmc_rengine/formats/mod.hpp"
#include "dmcresource/archive_entry.h"
#include "dmcresource/collision_debug.h"
#include "dmcresource/mod_bytes.h"
#include "dmcresource/shadow_hull.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/motion/uv_scroll.h"
#include "dmcresource/motion/skeleton_rig.h"
#include "dmcresource/spider/session_actions.h"

namespace dmcresource::pac_assembly {
namespace {

constexpr std::size_t kMaxNestingDepth = 3U;

struct Entry final {
    std::size_t archive{};
    std::size_t depth{};      // 0 = directly in the archive, >0 = nested
    bool effect_bank{};       // inside a PNST nested in the archive
    std::string name;
    std::string container;   // "" for the top-level archive
    std::optional<std::uint32_t> slot;
    archive::EntryKind kind;
    const std::vector<std::uint8_t>* bytes{};
};

[[nodiscard]] std::optional<std::uint32_t> slot_of(const ChildResource& child) {
    constexpr std::string_view prefix = "slot-";
    if (child.id.rfind(prefix, 0) != 0U) return std::nullopt;
    std::uint32_t value = 0U;
    for (std::size_t i = prefix.size(); i < child.id.size(); ++i) {
        const char c = child.id[i];
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return value;
}

void collect(const Session& container,
             std::size_t archive,
             const std::string& prefix,
             std::size_t depth,
             bool effect_bank,
             std::vector<std::unique_ptr<Session>>* nested_owner,
             std::vector<Entry>* out,
             AssemblyReport* report) {
    for (const auto& child : container.children) {
        if (child.source_bytes.empty()) continue;
        const auto kind = archive::classify_payload(child.source_bytes.data(),
                                                    child.source_bytes.size());
        const std::string name = prefix + child.suggested_filename;
        if (kind.format == Format::Pac || kind.format == Format::Pnst || kind.format == Format::EffectBank) {
            if (depth >= kMaxNestingDepth) continue;
            auto nested = open_session(name, child.source_bytes.data(), child.source_bytes.size());
            // A rejected archive opens as a raw binary view without children.
            if (!nested || nested->children.empty()) continue;
            ++report->nested_archives;
            nested_owner->push_back(std::move(nested));
            // A PNST nested in an archive is an effect bank: CEm028 hands its
            // slot 9 to 0x1402C04C0 (slot 0 table + slot 1 resource PNST),
            // never to a model loader; weapon PNSTs keep trails in slot 2.
            collect(*nested_owner->back(), archive, name + "/", depth + 1U,
                    effect_bank || kind.format == Format::Pnst || kind.format == Format::EffectBank,
                    nested_owner, out, report);
            continue;
        }
        if (kind.shadow) ++report->shadows;
        out->push_back({archive, depth, effect_bank, name, prefix, slot_of(child), kind,
                        &child.source_bytes});
    }
}

// Nearest PTX before `index` in the same container, else the nearest after it.
[[nodiscard]] std::optional<std::size_t> texture_for(const std::vector<Entry>& entries,
                                                     std::size_t index) {
    const auto& container = entries[index].container;
    const auto archive = entries[index].archive;
    const auto same = [&](const Entry& e) {
        return e.archive == archive && e.container == container && e.kind.format == Format::Ptx;
    };
    for (std::size_t i = index; i-- > 0U;) {
        if (same(entries[i])) return i;
    }
    for (std::size_t i = index + 1U; i < entries.size(); ++i) {
        if (same(entries[i])) return i;
    }
    // Modded archives sometimes keep the PTX in another container of the
    // same archive: fall back to the nearest PTX anywhere in that archive.
    const auto any = [&](const Entry& e) {
        return e.archive == archive && e.kind.format == Format::Ptx;
    };
    for (std::size_t i = index; i-- > 0U;) {
        if (any(entries[i])) return i;
    }
    for (std::size_t i = index + 1U; i < entries.size(); ++i) {
        if (any(entries[i])) return i;
    }
    return std::nullopt;
}

// Enemy motions: bind each id group of the script to a motion PAC and label
// every MOT with the actions that play it (table B, 0x14005A360).
void label_enemy_motions(const motion::MotionScriptFile& script,
                         std::string_view archive_name,
                         std::vector<Session::MotionPayload>& motions,
                         std::string* detail) {
    std::vector<motion::MotionPack> packs;
    for (const auto& m : motions) {
        if (m.pack_slot < 0 || m.mot_slot < 0) continue;
        const auto slot = static_cast<std::uint32_t>(m.pack_slot);
        auto it = std::find_if(packs.begin(), packs.end(),
                               [slot](const motion::MotionPack& p) { return p.archive_slot == slot; });
        if (it == packs.end()) {
            packs.push_back({slot, {}});
            it = std::prev(packs.end());
        }
        it->slots.push_back(static_cast<std::uint32_t>(m.mot_slot));
    }
    std::sort(packs.begin(), packs.end(),
              [](const motion::MotionPack& a, const motion::MotionPack& b) {
                  return a.archive_slot < b.archive_slot;
              });
    const auto groups = motion::bind_motion_groups(script, packs, archive_name);
    for (const auto& g : groups) {
        if (detail != nullptr) {
            *detail += " motionGroup" + std::to_string(g.group) + "=" +
                (g.archive_slot ? "slot" + std::to_string(*g.archive_slot) : std::string{"unbound"}) +
                (g.exe_confirmed ? "(exe)" : "(data)");
        }
        if (!g.archive_slot) continue;
        for (auto& m : motions) {
            if (m.pack_slot != static_cast<int>(*g.archive_slot) || m.mot_slot < 0) continue;
            if (!m.actions.empty()) continue;  // first (lowest) group wins the label
            const auto id = static_cast<std::uint16_t>(g.group * 100U + static_cast<unsigned>(m.mot_slot));
            const auto actions = script.actions_for(id);
            if (actions.empty()) continue;
            std::string label = "act";
            bool loop = false;
            for (std::size_t k = 0U; k < actions.size(); ++k) {
                if (k == 6U) {
                    label += ",+" + std::to_string(actions.size() - 6U);
                    break;
                }
                label += (k == 0U ? " " : ",") + std::to_string(actions[k].action);
            }
            for (const auto& r : script.resources(actions.front().bank, actions.front().action)) {
                if (r.id == id && r.loop == 1U) loop = true;
            }
            if (loop) label += " loop";
            m.actions = label;
            m.name = label + " · " + m.name;
        }
    }
}

[[nodiscard]] bool player_archive(std::string_view name) noexcept {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1U);
    return name.size() >= 2U &&
           std::tolower(static_cast<unsigned char>(name[0])) == 'p' &&
           std::tolower(static_cast<unsigned char>(name[1])) == 'l';
}

// Drop the triangles of `objects` (MeshPrimitive::object_index) of one
// composite part; vertices stay so skinning and attachments keep their ranges.
std::size_t hide_part_objects(Session* session, std::size_t part_index,
                              std::span<const std::uint32_t> objects) {
    if (session == nullptr || part_index >= session->composite_parts.size()) return 0U;
    std::size_t begin = 0U;
    for (std::size_t part = 0U; part < part_index; ++part) {
        for (const auto& primitive : session->composite_parts[part].scene.meshes) {
            begin += primitive.mesh.vertices.size();
        }
    }
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    std::size_t cursor = begin;
    for (const auto& primitive : session->composite_parts[part_index].scene.meshes) {
        const auto count = primitive.mesh.vertices.size();
        for (const auto object : objects) {
            if (primitive.object_index == object) ranges.push_back({cursor, cursor + count});
        }
        cursor += count;
    }
    if (ranges.empty()) return 0U;
    const auto hidden = [&ranges](std::uint32_t vertex) {
        for (const auto& [lo, hi] : ranges) {
            if (vertex >= lo && vertex < hi) return true;
        }
        return false;
    };
    auto& indices = session->render_mesh.indices;
    auto& slots = session->render_triangle_texture_slots;
    const bool slotted = slots.size() * 3U == indices.size();
    std::vector<std::uint32_t> kept_indices;
    std::vector<std::uint32_t> kept_slots;
    std::size_t removed = 0U;
    for (std::size_t t = 0U; t + 2U < indices.size(); t += 3U) {
        if (hidden(indices[t]) || hidden(indices[t + 1U]) || hidden(indices[t + 2U])) {
            ++removed;
            continue;
        }
        kept_indices.insert(kept_indices.end(), indices.begin() + static_cast<std::ptrdiff_t>(t),
                            indices.begin() + static_cast<std::ptrdiff_t>(t + 3U));
        if (slotted) kept_slots.push_back(slots[t / 3U]);
    }
    indices = std::move(kept_indices);
    if (slotted) slots = std::move(kept_slots);
    return removed;
}

}  // namespace

std::unique_ptr<Session> assemble_pac(const Session& pac,
                                      AssemblyReport* report_out,
                                      std::string_view archive_name,
                                      std::size_t enemy_variant) noexcept {
    const Session* archives[] = {&pac};
    const std::string_view names[] = {archive_name};
    return assemble_archives(archives, names, report_out, enemy_variant);
}

std::unique_ptr<Session> assemble_archives(std::span<const Session* const> archives,
                                           std::span<const std::string_view> archive_names,
                                           AssemblyReport* report_out,
                                           std::size_t enemy_variant) noexcept {
    AssemblyReport report;
    try {
        if (archives.empty() || archives.size() != archive_names.size()) return nullptr;
        for (const auto* archive : archives) {
            if (archive == nullptr ||
                (archive->probe.format != Format::Pac && archive->probe.format != Format::Pnst)) {
                return nullptr;
            }
        }
        const Session& pac = *archives.front();
        const std::string_view archive_name = archive_names.front();
        std::vector<std::unique_ptr<Session>> nested;
        std::vector<Entry> entries;
        for (std::size_t a = 0U; a < archives.size(); ++a) {
            const std::string prefix = a == 0U ? std::string{} : std::string{archive_names[a]} + "/";
            // Added archives keep their own container key so pairing never
            // crosses archives; archive 0 keeps "" for the player slot rule.
            collect(*archives[a], a, prefix, 0U, false, &nested, &entries, &report);
        }

        const auto positions = motion::archive_variants(archive_name);
        const motion::ArchiveVariant* position =
            positions.empty() ? nullptr
                              : &positions[std::min(enemy_variant, positions.size() - 1U)];
        const motion::EnemyVariant* variant = position != nullptr ? position->enemy : nullptr;
        const std::uint32_t weapon_slot = variant == nullptr ? 0U
            : (position->alternate_weapon ? variant->weapon_slot_alt : variant->weapon_slot);
        const std::uint32_t cloth_count =
            variant == nullptr || (variant->cloth_only_first_variant && position->alternate_weapon)
                ? 0U
                : variant->cloth_count;
        if (position != nullptr) report.enemy_class = position->label;
        std::vector<std::unique_ptr<Session>> models;
        std::vector<std::string> model_names;
        std::vector<std::size_t> model_entry;
        std::vector<std::optional<std::size_t>> texture_for_model;
        std::vector<Session::MotionPayload> motions;

        for (std::size_t index = 0U; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (entry.kind.format == Format::Mod && entry.effect_bank) {
                // Effect models (slash trails, sparks, bat particles) are
                // spawned by the effect system, not loaded as actor models.
                ++report.effect_models_skipped;
                continue;
            }
            if (entry.kind.format == Format::Mod && variant != nullptr && entry.archive == 0U &&
                entry.container.empty()) {
                // Shared enemy archive: keep only this class's body, cloth and weapon.
                bool used = entry.slot == variant->body_slot || entry.slot == weapon_slot;
                for (std::uint32_t c = 0U; c < cloth_count; ++c) {
                    used = used || entry.slot == variant->cloth[c].slot;
                }
                if (!used) {
                    ++report.variant_models_skipped;
                    continue;
                }
            }
            if (entry.kind.format == Format::Mod) {
                auto model = open_session(entry.name, entry.bytes->data(), entry.bytes->size());
                if (!model || !model->renderable) continue;
                models.push_back(std::move(model));
                model_names.push_back(entry.name);
                model_entry.push_back(index);
                std::optional<std::size_t> texture = texture_for(entries, index);
                if (variant != nullptr && variant->texture_slot != motion::kNoEnemySlot &&
                    entry.archive == 0U && entry.container.empty() &&
                    entry.slot == variant->body_slot) {
                    for (std::size_t t = 0U; t < entries.size(); ++t) {
                        if (entries[t].archive == 0U && entries[t].container.empty() &&
                            entries[t].slot == variant->texture_slot &&
                            entries[t].kind.format == Format::Ptx) {
                            texture = t;
                        }
                    }
                }
                texture_for_model.push_back(texture);
            } else if (entry.kind.format == Format::Mot) {
                // Shared enemy archive: only the motion PACs this class reads.
                if (variant != nullptr && variant->motion_slots[0] != 0U && entry.archive == 0U &&
                    entry.depth == 1U && entry.container.rfind("slot_", 0) == 0U) {
                    std::uint32_t pack = 0U;
                    try {
                        pack = static_cast<std::uint32_t>(std::stoul(entry.container.substr(5U, 4U)));
                    } catch (...) {
                        pack = 0U;
                    }
                    const auto& wanted = variant->motion_slots;
                    if (std::find(wanted.begin(), wanted.end(), pack) == wanted.end()) {
                        ++report.variant_motions_skipped;
                        continue;
                    }
                }
                // Weapon banks (pl000_00_N.pac) are labelled with their weapon.
                const auto bank = entry.archive < archive_names.size()
                    ? motion::weapon_motion_bank(archive_names[entry.archive])
                    : std::nullopt;
                Session::MotionPayload payload{
                    bank ? std::string{bank->weapon_name} + " · " + entry.name : entry.name,
                    *entry.bytes};
                // Motion script address: pl000.pac slots 2/3/4 hold banks 0/1/2
                // (pl000_00_0..2), an added pl000_00_<N>.pac is bank N; the MOT
                // index is its slot.
                if (entry.slot) {
                    std::optional<std::size_t> script_bank;
                    if (entry.archive == 0U && player_archive(archive_name) && entry.depth == 1U) {
                        for (std::uint32_t k = 2U; k <= 4U; ++k) {
                            if (entry.container == archive::slot_filename(k, {Format::Pac, "PAC", "pac"}) + "/") {
                                script_bank = k - 2U;
                            }
                        }
                    } else if (entry.archive < archive_names.size() && entry.depth == 0U) {
                        script_bank = motion::player_motion_bank(archive_names[entry.archive]);
                    }
                    if (script_bank) {
                        payload.bank = static_cast<int>(*script_bank);
                        payload.index = static_cast<int>(*entry.slot);
                    }
                }
                if (entry.slot && entry.depth == 1U && entry.container.size() > 5U &&
                    entry.container.rfind("slot_", 0) == 0U) {
                    try {
                        payload.pack_slot = std::stoi(entry.container.substr(5U, 4U));
                        payload.mot_slot = static_cast<int>(*entry.slot);
                    } catch (...) {
                        payload.pack_slot = -1;
                    }
                }
                motions.push_back(std::move(payload));
            }
        }
        if (models.empty()) {
            report.detail = "PAC assembly: archive holds no renderable MOD";
            if (report_out != nullptr) *report_out = std::move(report);
            return nullptr;
        }

        std::unique_ptr<Session> assembled;
        if (models.size() == 1U) {
            assembled = std::move(models.front());
            if (texture_for_model.front().has_value()) {
                const auto& ptx = entries[*texture_for_model.front()];
                if (spider::actions::attach_ptx(assembled.get(), ptx.name,
                                                ptx.bytes->data(), ptx.bytes->size())) {
                    ++report.textures_attached;
                }
            } else {
                ++report.textures_unpaired;
            }
        } else {
            std::vector<const Session*> parts;
            parts.reserve(models.size());
            for (const auto& model : models) parts.push_back(model.get());
            // -1: no inferred host; parts start in their source coordinates.
            assembled = spider::actions::compose_mod_sessions(parts, model_names, -1);
            if (!assembled) {
                report.detail = "PAC assembly: MOD parts could not be composed";
                if (report_out != nullptr) *report_out = std::move(report);
                return nullptr;
            }

            // One PTX for every part (player PACs: slot 0) -> one shared bank.
            bool shared = texture_for_model.front().has_value();
            for (const auto& texture : texture_for_model) {
                shared = shared && texture == texture_for_model.front();
            }
            if (shared) {
                const auto& ptx = entries[*texture_for_model.front()];
                if (spider::actions::attach_ptx(assembled.get(), ptx.name,
                                                ptx.bytes->data(), ptx.bytes->size())) {
                    report.textures_attached = models.size();
                }
            } else {
                for (std::size_t part = 0U; part < texture_for_model.size(); ++part) {
                    if (!texture_for_model[part].has_value()) {
                        ++report.textures_unpaired;
                        continue;
                    }
                    const auto& ptx = entries[*texture_for_model[part]];
                    if (spider::actions::attach_ptx_to_part(
                            assembled.get(), static_cast<int>(part), ptx.name,
                            ptx.bytes->data(), ptx.bytes->size())) {
                        ++report.textures_attached;
                    }
                }
            }

            // Body: archive 0 top-level slot 1 for player PACs, else its first MOD.
            std::optional<std::size_t> body;
            std::optional<std::size_t> coat;
            const bool player = player_archive(archive_name);
            for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                const auto& entry = entries[model_entry[part]];
                if (entry.archive != 0U) continue;
                if (!body && !player) body = part;
                if (!player || !entry.container.empty() || !entry.slot.has_value()) continue;
                if (*entry.slot == motion::kPlayerBodySlot) body = part;
                if (*entry.slot == motion::kPlayerCoatSlot) coat = part;
            }
            // IPlayer coat: top-level slot 12 hangs from body joint 3 (or
            // 3 + coat header +0x13 under the coat-joint EXE patch).
            if (body && coat) {
                const auto& coat_bytes = *entries[model_entry[*coat]].bytes;
                const auto body_joints =
                    assembled->composite_parts[*body].scene.nodes.size();
                const auto host = motion::player_coat_host_joint(coat_bytes, body_joints);
                if (motion::attach_part_skeleton(assembled.get(), *body, *coat, host, true)) {
                    ++report.attached_parts;
                    report.detail_attachments += " coat=slot12->bodyJoint" + std::to_string(host);
                    if (host != motion::kPlayerCoatHostJoint) {
                        report.detail_attachments += "(coat+0x13 patch)";
                    }
                    // Slot 15 node constraints (coat patch, hook 0x140215373).
                    for (const auto& e : entries) {
                        if (e.archive != 0U || !e.container.empty() ||
                            e.slot != motion::kPlayerCoatConstraintSlot) {
                            continue;
                        }
                        const auto constraints = motion::parse_coat_constraints(*e.bytes);
                        if (!constraints.empty() &&
                            motion::set_part_node_constraints(assembled.get(), *coat, constraints)) {
                            report.detail_attachments += " coatConstraints=" +
                                std::to_string(constraints.size()) + "(slot15)";
                        }
                        break;
                    }
                }
            }
            // Enemy node constraints (CEm028 init 0x140130480): top-level part
            // slots follow body joints node by node.
            for (std::size_t part = 0U; !player && part < model_entry.size(); ++part) {
                const auto& entry = entries[model_entry[part]];
                if (entry.archive != 0U || !entry.container.empty() || !entry.slot) continue;
                const auto record = motion::enemy_constraints_for(archive_name, *entry.slot);
                if (!record) continue;
                for (std::size_t host = 0U; host < model_entry.size(); ++host) {
                    const auto& host_entry = entries[model_entry[host]];
                    if (host_entry.archive != 0U || !host_entry.container.empty() ||
                        host_entry.slot != record->body_slot) {
                        continue;
                    }
                    if (motion::attach_part_nodes(assembled.get(), host, part,
                                                  record->constraints)) {
                        ++report.attached_parts;
                        report.detail_attachments += " slot" + std::to_string(*entry.slot) +
                            "->bodyJoints(" + std::to_string(record->constraints.size()) + ")";
                    }
                    break;
                }
            }
            // Enemy class: cloth models on their body joints, weapon on joint 9.
            if (variant != nullptr) {
                std::optional<std::size_t> host;
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& entry = entries[model_entry[part]];
                    if (entry.archive == 0U && entry.container.empty() &&
                        entry.slot == variant->body_slot) {
                        host = part;
                    }
                }
                for (std::size_t part = 0U; host && part < model_entry.size(); ++part) {
                    const auto& entry = entries[model_entry[part]];
                    if (entry.archive != 0U || !entry.container.empty() || !entry.slot) continue;
                    for (std::uint32_t c = 0U; c < cloth_count; ++c) {
                        if (*entry.slot != variant->cloth[c].slot) continue;
                        if (motion::attach_part_skeleton(assembled.get(), *host, part,
                                                         variant->cloth[c].host_joint, false)) {
                            ++report.attached_parts;
                            report.detail_attachments += " cloth slot" + std::to_string(*entry.slot) +
                                "->bodyJoint" + std::to_string(variant->cloth[c].host_joint);
                        }
                    }
                    if (*entry.slot == weapon_slot &&
                        motion::attach_part_skeleton(
                            assembled.get(), *host, part, variant->weapon_joint, false,
                            motion::attach_local_matrix_zyx(variant->weapon_translation,
                                                            variant->weapon_rotation_zyx))) {
                        ++report.attached_parts;
                        report.detail_attachments += " weapon slot" + std::to_string(*entry.slot) +
                            "->bodyJoint" + std::to_string(variant->weapon_joint);
                    }
                }
            }
            // Chains (.clt text slots): player coat slot 12 <- slot 13, enemy
            // model slots per kEnemyClothSources; solver 0x1402C9450.
            {
                const auto clt_text = [&](std::uint32_t slot) -> std::string_view {
                    for (const auto& e : entries) {
                        if (e.archive != 0U || !e.container.empty() || e.slot != slot) continue;
                        const auto& bytes = *e.bytes;
                        if (bytes.empty() || bytes.front() != ';') return {};
                        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
                    }
                    return {};
                };
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& entry = entries[model_entry[part]];
                    if (entry.archive != 0U || !entry.container.empty() || !entry.slot) continue;
                    std::optional<std::uint32_t> clt_slot;
                    if (player) {
                        if (coat && part == *coat) clt_slot = motion::kPlayerCoatClothSlot;
                    } else {
                        clt_slot = motion::enemy_cloth_slot(archive_name, *entry.slot);
                    }
                    if (!clt_slot) continue;
                    const auto text = clt_text(*clt_slot);
                    if (text.empty()) continue;
                    // Chains with collision (0x1402CA2F0): IPlayer coats on the
                    // body capsules, CEm028 hair on the neck/head capsules.
                    std::span<const motion::ClothCapsule> capsules;
                    if (player) {
                        capsules = motion::kPlayerCoatCapsules;
                    } else if (*entry.slot == 4U && *clt_slot == 7U &&
                               motion::enemy_constraints_for(archive_name, 4U).has_value()) {
                        capsules = motion::kEm028HairCapsules;
                    }
                    const auto nodes =
                        motion::attach_part_cloth(assembled.get(), part, text, 60U, capsules);
                    if (nodes > 0U) {
                        ++report.cloth_parts;
                        report.detail_attachments += " cloth slot" + std::to_string(*entry.slot) +
                            "<-clt" + std::to_string(*clt_slot) + "(" + std::to_string(nodes) +
                            " nodes)";
                    }
                }
            }
            // Model objects the selected position does not draw (MOD object bit 0).
            if (position != nullptr && position->hide_count > 0U) {
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& entry = entries[model_entry[part]];
                    if (entry.archive != 0U || !entry.container.empty() ||
                        entry.slot != position->hide_slot) {
                        continue;
                    }
                    const auto hidden = hide_part_objects(
                        assembled.get(), part,
                        std::span<const std::uint32_t>{position->hide_objects.data(),
                                                       position->hide_count});
                    report.detail_attachments += " hidden slot" + std::to_string(*entry.slot) +
                        " objects(" + std::to_string(hidden) + " triangles)";
                }
            }
            // Added weapon archives: every MOD hangs from its record's joint.
            for (std::size_t a = 1U; body && a < archive_names.size(); ++a) {
                const auto record = motion::weapon_record_for_archive(archive_names[a]);
                if (!record) continue;
                const auto offset = motion::weapon_offset_matrix(*record);
                const auto second = motion::weapon_second_part(record->class_name);
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    if (entries[model_entry[part]].archive != a) continue;
                    if (second) {
                        // One MOD, two blades on their own nodes (0x140227CF0).
                        const std::array<CompositeNodeConstraint, 2> nodes{{
                            {second->first_node, record->joint, offset},
                            {second->second_node, second->joint,
                             motion::attach_local_matrix(second->translation,
                                                         second->rotation_xyz_radians)},
                        }};
                        if (motion::attach_part_nodes(assembled.get(), *body, part, nodes)) {
                            ++report.attached_parts;
                            assembled->weapon_bindings.push_back({part, record->class_name, 0U});
                            report.detail_attachments += " " + std::string{record->class_name} +
                                "->bodyJoint" + std::to_string(record->joint) + "(node" +
                                std::to_string(second->first_node) + "+node" +
                                std::to_string(second->second_node) + ")";
                            continue;
                        }
                    }
                    if (motion::attach_part_skeleton(assembled.get(), *body, part,
                                                     record->joint, false, offset)) {
                        ++report.attached_parts;
                        assembled->weapon_bindings.push_back({part, record->class_name, 0U});
                        report.detail_attachments += " " + std::string{record->class_name} +
                            "->bodyJoint" + std::to_string(record->joint);
                    }
                }
            }
        }

        // Texture scrolls (.tsc, CDrawUV): objects whose source flags carry a
        // scroll number move their UVs with the playback clock. A single model
        // is the session itself (no composite parts).
        const auto part_meshes = [&assembled](std::size_t part) -> const std::vector<MeshPrimitive>& {
            return assembled->composite_parts.empty() ? assembled->scene.meshes
                                                      : assembled->composite_parts[part].scene.meshes;
        };
        for (std::size_t part = 0U; part < model_entry.size(); ++part) {
            const auto& entry = entries[model_entry[part]];
            if (entry.archive != 0U || !entry.container.empty() || !entry.slot) continue;
            const auto tsc_slot = motion::tsc_slot_for(archive_name, *entry.slot);
            if (!tsc_slot) continue;
            std::vector<motion::ScrollRecord> records;
            for (const auto& e : entries) {
                if (e.archive != 0U || !e.container.empty() || e.slot != *tsc_slot) continue;
                records = motion::parse_tsc(
                    {reinterpret_cast<const char*>(e.bytes->data()), e.bytes->size()});
            }
            if (records.empty()) continue;
            const ModBytes view{entry.bytes->data(), entry.bytes->size()};
            const auto parsed = dmc::rengine::formats::mod::Parser::parse(view.span());
            if (!parsed.ok()) continue;
            const auto& objects = parsed.document.outer_models;
            std::size_t cursor = 0U;
            for (std::size_t p = 0U; p < part; ++p) {
                for (const auto& primitive : part_meshes(p)) {
                    cursor += primitive.mesh.vertices.size();
                }
            }
            std::size_t bound = 0U;
            for (const auto& primitive : part_meshes(part)) {
                const auto begin = cursor;
                const auto count = primitive.mesh.vertices.size();
                cursor += count;
                if (primitive.object_index >= objects.size()) continue;
                const auto& object = objects[primitive.object_index];
                const int number = motion::object_scroll_number(object.source_flags);
                if (number < 0) continue;
                const motion::ScrollRecord* record = nullptr;
                for (const auto& candidate : records) {
                    if (candidate.number == number) record = &candidate;
                }
                if (record == nullptr || primitive.mesh_index >= object.meshes.size()) continue;
                if (record->texture >= 0 &&
                    object.meshes[primitive.mesh_index].texture_slot !=
                        static_cast<std::uint16_t>(record->texture)) {
                    continue;
                }
                const auto& uv = assembled->render_mesh.uv0;
                if (begin + count > uv.size()) continue;
                motion::UvScrollBinding binding;
                binding.vertex_begin = begin;
                binding.rest_uv.assign(uv.begin() + static_cast<std::ptrdiff_t>(begin),
                                       uv.begin() + static_cast<std::ptrdiff_t>(begin + count));
                binding.record = *record;
                binding.state = motion::start_scroll(*record);
                // JntNo (type 10 facing): defaults to MOD +0x13, clamped to
                // the node count (0x14030AFC0).
                std::size_t node_begin = 0U;
                for (std::size_t p = 0U; p < part && !assembled->composite_parts.empty(); ++p) {
                    node_begin += assembled->composite_parts[p].scene.nodes.size();
                }
                const auto node_count = parsed.document.header.transform_domain_count;
                const auto joint = record->joint >= 0 && record->joint <= node_count
                    ? static_cast<std::size_t>(record->joint)
                    : static_cast<std::size_t>(parsed.document.header.default_joint_index());
                binding.joint_node = node_begin + joint;
                assembled->uv_scrolls.push_back(std::move(binding));
                ++bound;
            }
            if (bound > 0U) {
                ++report.uv_scroll_parts;
                report.detail_attachments += " tsc slot" + std::to_string(*entry.slot) +
                    "<-slot" + std::to_string(*tsc_slot) + "(" + std::to_string(bound) +
                    " meshes)";
            }
        }
        // SHW: pair each shadow file with the MOD of the same archive and
        // container whose node count matches header +0x11 (nearest MOD before
        // it wins); its hulls then follow that model's joints.
        {
            std::vector<std::size_t> node_begin(models.size(), 0U);
            std::vector<std::size_t> node_count(models.size(), 0U);
            std::vector<std::shared_ptr<const motion::SkeletonRig>> rigs(models.size());
            if (models.size() == 1U) {
                node_count[0] = assembled->scene.nodes.size();
                rigs[0] = assembled->scene.rig;
            } else if (assembled->composite_parts.size() == models.size()) {
                std::size_t cursor = 0U;
                for (std::size_t part = 0U; part < models.size(); ++part) {
                    const auto& scene = assembled->composite_parts[part].scene;
                    node_begin[part] = cursor;
                    node_count[part] = scene.nodes.size();
                    rigs[part] = scene.rig;
                    cursor += scene.nodes.size();
                }
            }
            for (std::size_t index = 0U; index < entries.size(); ++index) {
                const auto& entry = entries[index];
                if (entry.kind.format != Format::Shw || entry.effect_bank) continue;
                auto hulls = shadow::parse_hulls(entry.bytes->data(), entry.bytes->size());
                if (!hulls || hulls->hulls.empty()) continue;
                std::optional<std::size_t> owner;
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& model = entries[model_entry[part]];
                    if (model.archive != entry.archive || model.container != entry.container ||
                        rigs[part] == nullptr || node_count[part] != hulls->node_count ||
                        rigs[part]->node_count() != node_count[part] ||
                        hulls->max_selector() >= node_count[part]) {
                        continue;
                    }
                    if (!owner || model_entry[part] < index) owner = part;
                }
                if (!owner) continue;
                shadow::ShadowBinding binding;
                binding.name = entry.name;
                binding.node_begin = node_begin[*owner];
                binding.node_count = node_count[*owner];
                binding.rig = rigs[*owner];
                binding.hulls = std::move(*hulls);
                assembled->shadow_bindings.push_back(std::move(binding));
                ++report.shadows_bound;
                report.detail_attachments += " " + entry.name + "->" + model_names[*owner];
            }
            // Collision handle (0x14005C260): the shape table of the top-level
            // archive and the index in the slot before it; attacks use the body
            // (first top-level MOD) bone matrices.
            for (std::size_t index = 0U; index < entries.size() && assembled->collision == nullptr; ++index) {
                const auto& shapes_entry = entries[index];
                if (shapes_entry.kind.format != Format::CollisionShapes || shapes_entry.archive != 0U ||
                    !shapes_entry.container.empty() || !shapes_entry.slot) {
                    continue;
                }
                const std::span<const std::uint8_t> shape_bytes{shapes_entry.bytes->data(),
                                                                shapes_entry.bytes->size()};
                const auto shapes = collision::parse_shapes(shape_bytes);
                const Entry* index_entry = nullptr;
                for (const auto& e : entries) {
                    if (e.archive != 0U || !e.container.empty() || !e.slot || *e.slot >= *shapes_entry.slot) continue;
                    if (index_entry == nullptr || *e.slot > *index_entry->slot) index_entry = &e;
                }
                if (index_entry == nullptr ||
                    !collision::looks_like_attack_index(
                        std::span<const std::uint8_t>{index_entry->bytes->data(), index_entry->bytes->size()},
                        shapes.size())) {
                    continue;
                }
                std::optional<std::size_t> owner;
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& model = entries[model_entry[part]];
                    if (model.archive != 0U || !model.container.empty()) continue;
                    if (!owner || model_entry[part] < model_entry[*owner]) owner = part;
                }
                if (!owner || node_count[*owner] == 0U) continue;
                auto binding = std::make_shared<collision::CollisionBinding>();
                binding->name = index_entry->name + "+" + shapes_entry.name;
                binding->attacks = collision::parse_attack_index(
                    std::span<const std::uint8_t>{index_entry->bytes->data(), index_entry->bytes->size()});
                binding->shapes = shapes;
                binding->node_begin = node_begin[*owner];
                binding->node_count = node_count[*owner];
                assembled->collision = std::move(binding);
                report.detail_attachments += " collision slot" + std::to_string(*index_entry->slot) + "+slot" +
                    std::to_string(*shapes_entry.slot) + "->" + model_names[*owner] + "(" +
                    std::to_string(collision::collision_attack_ids(*assembled).size()) + " attacks)";
            }
        }

        report.models = models.size();
        report.motions = motions.size();
        // Only motions that can drive this scene are offered (e.g. em000's
        // body motions are hidden on the CEm005Shl01 shell).
        {
            std::vector<Session::MotionPayload> drivable;
            drivable.reserve(motions.size());
            for (auto& m : motions) {
                if (motion::motion_can_drive(*assembled, m.bytes)) drivable.push_back(std::move(m));
            }
            if (drivable.size() != motions.size()) {
                report.detail_attachments += " motionsHidden=" + std::to_string(motions.size() - drivable.size());
            }
            motions = std::move(drivable);
            report.motions = motions.size();
        }
        assembled->motion_library = std::move(motions);
        // Motion script: IPlayer pl000.pac slot 5 (0x1401EF461); enemies bind
        // their own script slot (em028 slot 10 at 0x140131037, em000 slot 38
        // at 0x1400982D9). Identified by its tables (MotionScriptFile).
        for (const auto& e : entries) {
            if (e.archive != 0U || !e.container.empty() || !e.slot ||
                e.kind.format != Format::MotionScript) {
                continue;
            }
            auto script = motion::MotionScriptFile::parse(
                std::span<const std::uint8_t>{e.bytes->data(), e.bytes->size()});
            if (!script) continue;
            report.detail_attachments += " motionScript=slot" + std::to_string(*e.slot) + "(" +
                std::to_string(script->bank_count()) + " banks)";
            auto shared = std::make_shared<const motion::MotionScriptFile>(std::move(*script));
            if (!shared->nested()) label_enemy_motions(*shared, archive_name, assembled->motion_library,
                                                       &report.detail_attachments);
            assembled->motion_script = std::move(shared);
            break;
        }
        assembled->children = pac.children;
        (void)archive_name;
        if (!assembled->children.empty()) {
            assembled->capabilities |= capability(ResourceCapability::ChildResources);
        }
        report.detail =
            "PAC assembly (read-only): models=" + std::to_string(report.models) +
            " texturesAttached=" + std::to_string(report.textures_attached) +
            " texturesUnpaired=" + std::to_string(report.textures_unpaired) +
            " motions=" + std::to_string(report.motions) +
            " shadowRecords=" + std::to_string(report.shadows) +
            " shadowsBound=" + std::to_string(report.shadows_bound) +
            " cloth=" + std::to_string(report.cloth_parts) +
            " uvScroll=" + std::to_string(report.uv_scroll_parts) +
            " nestedArchives=" + std::to_string(report.nested_archives) +
            " attachedParts=" + std::to_string(report.attached_parts) +
            " effectModelsSkipped=" + std::to_string(report.effect_models_skipped) +
            (report.enemy_class.empty() ? std::string{}
                                        : " enemyClass=" + report.enemy_class +
                                              " otherClassModelsSkipped=" +
                                              std::to_string(report.variant_models_skipped)) +
            " ptxPairing=nearest-preceding-in-container" + report.detail_attachments;
        if (!assembled->detail.empty()) assembled->detail += "\n";
        assembled->detail += report.detail;
        if (!assembled->trace.empty()) assembled->trace += "\n";
        assembled->trace += "[OK] native.pac.assembly";
        if (report_out != nullptr) *report_out = std::move(report);
        return assembled;
    } catch (...) {
        if (report_out != nullptr) {
            *report_out = {};
            report_out->detail = "PAC assembly failed";
        }
        return nullptr;
    }
}

}  // namespace dmcresource::pac_assembly
