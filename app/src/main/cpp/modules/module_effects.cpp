#include "dmcresource/native_module.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/effect_bank.h"
#include "dmcresource/format_views.h"
#include "dmcresource/module_support.h"

namespace dmcresource {
namespace {

// Effect record budget for decoded thumbnails (RGBA pixels).
constexpr std::uint64_t kMaxThumbnailPixels = 4U * 1024U * 1024U;

[[nodiscard]] std::string record_filename(const effect_bank::Record& r) {
    char base[32];
    std::snprintf(base, sizeof(base), "%c%03u", r.kind, r.id);
    std::string name{base};
    if (r.kind == 'T') return name + ".dds";
    if (r.kind == 'M') {
        if (r.bytes.size() >= 4U && std::memcmp(r.bytes.data(), "EFM ", 4U) == 0) return name + ".efm";
        return name + ".mod";
    }
    name += ".fx";
    name.push_back(static_cast<char>(r.kind >= 'A' && r.kind <= 'Z' ? r.kind + 32 : 'x'));
    return name;
}

PipelineResult run_effect_bank_module(const NativeModule& module,
                                      std::string_view,
                                      const std::uint8_t* bytes,
                                      std::size_t size,
                                      const ProbeResult& probe) noexcept {
    try {
        const auto bank = effect_bank::parse_bank(std::span<const std::uint8_t>{bytes, size});
        if (!bank) return module_support::reject(probe, module.id, "not an effect bank");
        PipelineResult out;
        out.accepted = true;
        out.renderable = false;
        out.probe = probe;
        out.modules.push_back({"identity-probe", true});
        out.modules.push_back({"bounded-read-guard", true});
        out.modules.push_back({"canonical.effect-bank.manifest", true});
        out.modules.push_back({module.id, true});
        out.inspection.format = "FXBANK";
        out.inspection.root.id = "fxbank";
        out.inspection.root.title = "Effect bank";
        out.inspection.root.kind = InspectionKind::Document;
        out.inspection.root.source_span = SourceSpan{0U, size};

        std::map<char, std::vector<const effect_bank::Record*>> by_kind;
        for (const auto& r : bank->records) by_kind[r.kind].push_back(&r);
        const auto add = [&out](const char* key, std::string value, EvidenceLevel e) {
            out.inspection.root.properties.push_back({key, std::move(value), e});
        };
        add("Loader", "0x1402C04C0 (manifest tokens -> registrar per kind)", EvidenceLevel::ExeConfirmed);
        add("Records", std::to_string(bank->records.size()) + " named, " + std::to_string(bank->record_slots) +
                           " slots (M takes two: model + companion)",
            EvidenceLevel::ExeConfirmed);
        add("Manifest end", bank->terminated ? "'#' token" : "end of text", EvidenceLevel::ExeConfirmed);
        std::ostringstream detail;
        detail << "Effect bank | records=" << bank->records.size() << " slots=" << bank->record_slots;
        for (const auto& [kind, list] : by_kind) {
            InspectionNode node;
            node.id = std::string{"kind-"} + kind;
            node.title = std::string{kind} + " x" + std::to_string(list.size()) + " - " +
                         std::string{effect_bank::kind_name(kind)};
            node.kind = InspectionKind::Collection;
            char reg[32];
            std::snprintf(reg, sizeof(reg), "0x%llX",
                          static_cast<unsigned long long>(effect_bank::registrar(kind)));
            node.properties.push_back({"Registrar", effect_bank::registrar(kind) ? reg : "none",
                                       EvidenceLevel::ExeConfirmed});
            std::ostringstream ids;
            std::size_t shown = 0U;
            for (const auto* r : list) {
                if (shown++ == 200U) {
                    ids << " ...";
                    break;
                }
                ids << (shown == 1U ? "" : " ") << r->id << "(" << r->bytes.size() << "B)";
            }
            node.properties.push_back({"Ids", ids.str(), EvidenceLevel::DataConfirmed});
            out.inspection.root.children.push_back(std::move(node));
            detail << "\n  " << kind << ": " << list.size();
        }

        // Decoded textures of this bank by id (sprite views sample them).
        std::map<std::uint32_t, ImagePreview> textures;
        std::uint64_t thumbnail_pixels = 0U;
        std::size_t index = 0U;
        for (const auto& r : bank->records) {
            ++index;
            if (r.bytes.empty()) continue;
            ChildResource child;
            child.id = "fx-" + std::to_string(index - 1U);
            child.suggested_filename = record_filename(r);
            std::span<const std::uint8_t> payload = r.bytes;
            if (r.kind == 'T') {
                const auto dds = effect_bank::texture_dds(r);
                if (!dds.empty()) payload = dds;
            }
            child.source_bytes.assign(payload.begin(), payload.end());
            child.title = child.suggested_filename + " · " + std::to_string(r.bytes.size()) + " B";
            child.probe = dmcresource::probe(child.suggested_filename, child.source_bytes.data(),
                                             child.source_bytes.size());
            child.capabilities = capability(ResourceCapability::Inspection);
            child.detail = std::string{"Effect record "} + r.kind + " " + std::to_string(r.id) + " (slot " +
                           std::to_string(r.slot) + "): " + std::string{effect_bank::kind_name(r.kind)};
            child.trace = "[OK] canonical.effect-bank.manifest";
            child.inspection.format = child.probe.family;
            child.inspection.root.id = child.id;
            child.inspection.root.title = child.title;
            child.inspection.root.kind = InspectionKind::Document;
            if (r.kind == 'T' && child.probe.format == Format::Dds) {
                child.capabilities = child.capabilities | ResourceCapability::ImagePreview;
                auto decoded = run_decode_pipeline(child.suggested_filename, child.source_bytes.data(),
                                                   child.source_bytes.size());
                const auto pixels = static_cast<std::uint64_t>(decoded.image_preview.width) *
                                    decoded.image_preview.height;
                if (decoded.accepted && decoded.image_preview.available() &&
                    thumbnail_pixels + pixels <= kMaxThumbnailPixels) {
                    thumbnail_pixels += pixels;
                    textures[r.id] = decoded.image_preview;
                    child.image_preview = std::move(decoded.image_preview);
                }
            }
            out.children.push_back(std::move(child));
        }
        // Sprite animations: drawn over their texture when the bank holds it.
        std::size_t child_index = 0U;
        for (const auto& r : bank->records) {
            if (r.bytes.empty()) continue;
            auto& child = out.children[child_index++];
            if (r.kind != 'A') continue;
            const auto sprite = effect_bank::sprite_animation(r);
            if (!sprite) continue;
            const auto found = textures.find(sprite->texture);
            child.image_preview = views::render_sprite_view(
                *sprite, r.id, found != textures.end() ? &found->second : nullptr);
            child.capabilities = child.capabilities | ResourceCapability::ImagePreview;
            child.detail += "\nSprite: texture T" + std::to_string(sprite->texture) + ", " +
                            std::to_string(sprite->frames.size()) + " frames, frame time " +
                            std::to_string(sprite->frame_time) + (sprite->loop ? ", loop" : ", once");
        }
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule effect_bank_module() noexcept {
    return {"formats.effect-bank.reader", "FXBANK", Format::EffectBank, ModuleKind::Structural, false,
            run_effect_bank_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ChildResources |
                ResourceCapability::Container};
}

}  // namespace dmcresource
