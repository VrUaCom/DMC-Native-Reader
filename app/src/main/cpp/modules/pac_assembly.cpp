#include "dmcresource/pac_assembly.h"

#include <cctype>
#include <cstdint>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/archive_entry.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/spider/session_actions.h"

namespace dmcresource::pac_assembly {
namespace {

constexpr std::size_t kMaxNestingDepth = 3U;

struct Entry final {
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
             const std::string& prefix,
             std::size_t depth,
             std::vector<std::unique_ptr<Session>>* nested_owner,
             std::vector<Entry>* out,
             AssemblyReport* report) {
    for (const auto& child : container.children) {
        if (child.source_bytes.empty()) continue;
        const auto kind = archive::classify_payload(child.source_bytes.data(),
                                                    child.source_bytes.size());
        const std::string name = prefix + child.suggested_filename;
        if (kind.format == Format::Pac) {
            if (depth >= kMaxNestingDepth) continue;
            auto nested = open_session(name, child.source_bytes.data(), child.source_bytes.size());
            if (!nested) continue;
            ++report->nested_archives;
            nested_owner->push_back(std::move(nested));
            collect(*nested_owner->back(), name + "/", depth + 1U, nested_owner, out, report);
            continue;
        }
        if (kind.shadow) ++report->shadows;
        out->push_back({name, prefix, slot_of(child), kind, &child.source_bytes});
    }
}

// Nearest PTX before `index` in the same container, else the nearest after it.
[[nodiscard]] std::optional<std::size_t> texture_for(const std::vector<Entry>& entries,
                                                     std::size_t index) {
    const auto& container = entries[index].container;
    for (std::size_t i = index; i-- > 0U;) {
        if (entries[i].container == container && entries[i].kind.format == Format::Ptx) return i;
    }
    for (std::size_t i = index + 1U; i < entries.size(); ++i) {
        if (entries[i].container == container && entries[i].kind.format == Format::Ptx) return i;
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
                                      std::string_view archive_name) noexcept {
    AssemblyReport report;
    try {
        if (pac.probe.format != Format::Pac) return nullptr;
        std::vector<std::unique_ptr<Session>> nested;
        std::vector<Entry> entries;
        collect(pac, {}, 0U, &nested, &entries, &report);

        std::vector<std::unique_ptr<Session>> models;
        std::vector<std::string> model_names;
        std::vector<std::size_t> model_entry;
        std::vector<std::optional<std::size_t>> texture_for_model;
        std::vector<Session::MotionPayload> motions;

        for (std::size_t index = 0U; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (entry.kind.format == Format::Mod) {
                auto model = open_session(entry.name, entry.bytes->data(), entry.bytes->size());
                if (!model || !model->renderable) continue;
                models.push_back(std::move(model));
                model_names.push_back(entry.name);
                model_entry.push_back(index);
                texture_for_model.push_back(texture_for(entries, index));
            } else if (entry.kind.format == Format::Mot) {
                motions.push_back({entry.name, *entry.bytes});
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

            // IPlayer coat: top-level slot 12 hangs from body (slot 1) joint 3.
            if (player_archive(archive_name)) {
                std::optional<std::size_t> body;
                std::optional<std::size_t> coat;
                for (std::size_t part = 0U; part < model_entry.size(); ++part) {
                    const auto& entry = entries[model_entry[part]];
                    if (!entry.container.empty() || !entry.slot.has_value()) continue;
                    if (*entry.slot == motion::kPlayerBodySlot) body = part;
                    if (*entry.slot == motion::kPlayerCoatSlot) coat = part;
                }
                if (body && coat &&
                    motion::attach_part_skeleton(assembled.get(), *body, *coat,
                                                 motion::kPlayerCoatHostJoint, true)) {
                    ++report.attached_parts;
                }
            }
        }

        report.models = models.size();
        report.motions = motions.size();
        assembled->motion_library = std::move(motions);
        assembled->children = pac.children;
        if (!assembled->children.empty()) {
            assembled->capabilities |= capability(ResourceCapability::ChildResources);
        }
        report.detail =
            "PAC assembly (read-only): models=" + std::to_string(report.models) +
            " texturesAttached=" + std::to_string(report.textures_attached) +
            " texturesUnpaired=" + std::to_string(report.textures_unpaired) +
            " motions=" + std::to_string(report.motions) +
            " shadowRecords=" + std::to_string(report.shadows) +
            " nestedArchives=" + std::to_string(report.nested_archives) +
            " attachedParts=" + std::to_string(report.attached_parts) +
            " ptxPairing=nearest-preceding-in-container" +
            (report.attached_parts != 0U ? " coat=slot12->bodyJoint3" : "");
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
