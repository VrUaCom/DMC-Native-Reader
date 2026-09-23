#include "dmcresource/pac_assembly.h"

#include <cstdint>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/archive_entry.h"
#include "dmcresource/spider/session_actions.h"

namespace dmcresource::pac_assembly {
namespace {

constexpr std::size_t kMaxNestingDepth = 3U;

struct Entry final {
    std::string name;
    archive::EntryKind kind;
    const std::vector<std::uint8_t>* bytes{};
};

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
        out->push_back({name, kind, &child.source_bytes});
    }
}

}  // namespace

std::unique_ptr<Session> assemble_pac(const Session& pac, AssemblyReport* report_out) noexcept {
    AssemblyReport report;
    try {
        if (pac.probe.format != Format::Pac) return nullptr;
        std::vector<std::unique_ptr<Session>> nested;
        std::vector<Entry> entries;
        collect(pac, {}, 0U, &nested, &entries, &report);

        std::vector<std::unique_ptr<Session>> models;
        std::vector<std::string> model_names;
        std::vector<std::optional<std::size_t>> texture_for_model;
        std::optional<std::size_t> leading_texture;
        std::vector<Session::MotionPayload> motions;

        for (std::size_t index = 0U; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            switch (entry.kind.format) {
                case Format::Mod: {
                    auto model = open_session(entry.name, entry.bytes->data(), entry.bytes->size());
                    if (!model || !model->renderable) break;
                    models.push_back(std::move(model));
                    model_names.push_back(entry.name);
                    texture_for_model.emplace_back();
                    break;
                }
                case Format::Ptx:
                    if (!texture_for_model.empty() && !texture_for_model.back().has_value()) {
                        texture_for_model.back() = index;
                    } else if (texture_for_model.empty() && !leading_texture.has_value()) {
                        leading_texture = index;
                    } else {
                        ++report.textures_unpaired;
                    }
                    break;
                case Format::Mot:
                    motions.push_back({entry.name, *entry.bytes});
                    break;
                default:
                    break;
            }
        }
        if (models.empty()) {
            report.detail = "PAC assembly: archive holds no renderable MOD";
            if (report_out != nullptr) *report_out = std::move(report);
            return nullptr;
        }
        if (leading_texture.has_value() && !texture_for_model.front().has_value()) {
            texture_for_model.front() = leading_texture;
        } else if (leading_texture.has_value()) {
            ++report.textures_unpaired;
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
            }
        } else {
            std::vector<const Session*> parts;
            parts.reserve(models.size());
            for (const auto& model : models) parts.push_back(model.get());
            // -1: no host inference; every part keeps character model space.
            assembled = spider::actions::compose_mod_sessions(parts, model_names, -1);
            if (!assembled) {
                report.detail = "PAC assembly: MOD parts could not be composed";
                if (report_out != nullptr) *report_out = std::move(report);
                return nullptr;
            }
            for (std::size_t part = 0U; part < texture_for_model.size(); ++part) {
                if (!texture_for_model[part].has_value()) continue;
                const auto& ptx = entries[*texture_for_model[part]];
                if (spider::actions::attach_ptx_to_part(
                        assembled.get(), static_cast<int>(part), ptx.name,
                        ptx.bytes->data(), ptx.bytes->size())) {
                    ++report.textures_attached;
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
            " placement=model-space ptxPairing=slot-adjacency";
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
