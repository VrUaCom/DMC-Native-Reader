#include "dmcresource/pac_assembly.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/archive_entry.h"
#include "dmcresource/shadow_hull.h"
#include "dmcresource/motion/part_attachment.h"
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
        if (kind.format == Format::Pac || kind.format == Format::Pnst) {
            if (depth >= kMaxNestingDepth) continue;
            auto nested = open_session(name, child.source_bytes.data(), child.source_bytes.size());
            if (!nested) continue;
            ++report->nested_archives;
            nested_owner->push_back(std::move(nested));
            // A PNST nested in an archive is an effect bank: CEm028 hands its
            // slot 9 to 0x1402C04C0 (slot 0 table + slot 1 resource PNST),
            // never to a model loader; weapon PNSTs keep trails in slot 2.
            collect(*nested_owner->back(), archive, name + "/", depth + 1U,
                    effect_bank || kind.format == Format::Pnst, nested_owner, out, report);
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

[[nodiscard]] bool player_archive(std::string_view name) noexcept {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1U);
    return name.size() >= 2U &&
           std::tolower(static_cast<unsigned char>(name[0])) == 'p' &&
           std::tolower(static_cast<unsigned char>(name[1])) == 'l';
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

        const auto variants = motion::enemy_variants_for(archive_name);
        const motion::EnemyVariant* variant =
            variants.empty() ? nullptr : &variants[std::min(enemy_variant, variants.size() - 1U)];
        if (variant != nullptr) report.enemy_class = std::string{variant->class_name};
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
                bool used = entry.slot == variant->body_slot || entry.slot == variant->weapon_slot;
                for (std::uint32_t c = 0U; c < variant->cloth_count; ++c) {
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
                texture_for_model.push_back(texture_for(entries, index));
            } else if (entry.kind.format == Format::Mot) {
                // Weapon banks (pl000_00_N.pac) are labelled with their weapon.
                const auto bank = entry.archive < archive_names.size()
                    ? motion::weapon_motion_bank(archive_names[entry.archive])
                    : std::nullopt;
                motions.push_back({bank ? std::string{bank->weapon_name} + " · " + entry.name
                                        : entry.name,
                                   *entry.bytes});
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
            // IPlayer coat: top-level slot 12 hangs from body joint 3.
            if (body && coat &&
                motion::attach_part_skeleton(assembled.get(), *body, *coat,
                                             motion::kPlayerCoatHostJoint, true)) {
                ++report.attached_parts;
                report.detail_attachments += " coat=slot12->bodyJoint3";
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
                    for (std::uint32_t c = 0U; c < variant->cloth_count; ++c) {
                        if (*entry.slot != variant->cloth[c].slot) continue;
                        if (motion::attach_part_skeleton(assembled.get(), *host, part,
                                                         variant->cloth[c].host_joint, false)) {
                            ++report.attached_parts;
                            report.detail_attachments += " cloth slot" + std::to_string(*entry.slot) +
                                "->bodyJoint" + std::to_string(variant->cloth[c].host_joint);
                        }
                    }
                    if (*entry.slot == variant->weapon_slot &&
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
                        report.detail_attachments += " " + std::string{record->class_name} +
                            "->bodyJoint" + std::to_string(record->joint);
                    }
                }
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
        }

        report.models = models.size();
        report.motions = motions.size();
        assembled->motion_library = std::move(motions);
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
