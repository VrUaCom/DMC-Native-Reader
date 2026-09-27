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

// Internal decoder hint only. FXBANK does not store historical child filenames:
 // the canonical loader addresses records by manifest kind + numeric id and
 // physical PNST slot. Keep UI labels separate from these neutral probe names.
[[nodiscard]] std::string record_probe_filename(const effect_bank::Record& r) {
    char base[48];
    std::snprintf(base, sizeof(base), "slot_%04u", r.slot);
    std::string name{base};
    if (r.kind == 'T') return name + ".dds";
    if (r.kind == 'M') {
        if (r.bytes.size() >= 4U && std::memcmp(r.bytes.data(), "EFM ", 4U) == 0) {
            return name + ".efm";
        }
        if (r.bytes.size() >= 4U && std::memcmp(r.bytes.data(), "MOD", 3U) == 0) {
            return name + ".mod";
        }
    }
    return name + ".bin";
}

[[nodiscard]] std::string record_format_label(const effect_bank::Record& r) {
    if (r.kind == 'T' && !effect_bank::texture_dds(r).empty()) return "DDS";
    if (r.kind == 'M') {
        if (r.bytes.size() >= 4U && std::memcmp(r.bytes.data(), "EFM ", 4U) == 0) return "EFM";
        if (r.bytes.size() >= 4U && std::memcmp(r.bytes.data(), "MOD", 3U) == 0) return "MOD";
    }
    return {};
}

[[nodiscard]] std::string record_title(const effect_bank::Record& r) {
    std::string title;
    title.push_back(r.kind);
    title += " " + std::to_string(r.id);
    const auto format = record_format_label(r);
    if (!format.empty()) title += " · " + format;
    title += " · " + std::to_string(r.bytes.size()) + " B";
    return title;
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
        std::map<std::pair<char, std::uint32_t>, std::size_t> child_by_key;
        std::uint64_t thumbnail_pixels = 0U;
        std::size_t index = 0U;
        for (const auto& r : bank->records) {
            ++index;
            if (r.bytes.empty()) continue;
            ChildResource child;
            child.id = "fx-" + std::to_string(index - 1U);
            child.suggested_filename = record_probe_filename(r);
            std::span<const std::uint8_t> payload = r.bytes;
            if (r.kind == 'T') {
                const auto dds = effect_bank::texture_dds(r);
                if (!dds.empty()) payload = dds;
            }
            child.source_bytes.assign(payload.begin(), payload.end());
            child.title = record_title(r);
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
            child_by_key[{r.kind, r.id}] = out.children.size();
            out.children.push_back(std::move(child));
        }
        // Bank-local A records are reusable by E records. Keep the parsed
        // animation keyed by its manifest id; this mirrors the EXE's A manager
        // lookup rather than deriving identity from a synthetic filename.
        std::map<std::uint32_t, effect_bank::SpriteAnimation> animations;
        for (const auto& r : bank->records) {
            if (r.kind != 'A' || r.bytes.empty()) continue;
            if (auto sprite = effect_bank::sprite_animation(r)) {
                animations.emplace(r.id, std::move(*sprite));
            }
        }

        // Every registered effect record gets two native surfaces when the
        // runtime structure is understood:
        //   Visual = texture/graph/parameter reconstruction from confirmed links
        //   Info   = the EXE-backed diagnostic view with offsets/fields.
        // T remains the real decoded texture; M remains the ordinary MOD/EFM 3D
        // renderer. Their generic info is already available through the reader's
        // normal information panel.
        for (const auto& r : bank->records) {
            if (r.bytes.empty() || r.kind == 'T' || r.kind == 'M') continue;
            const auto child_found = child_by_key.find({r.kind, r.id});
            if (child_found == child_by_key.end()) continue;
            auto& child = out.children[child_found->second];

            if (r.kind == 'A') {
                const auto animation = animations.find(r.id);
                if (animation == animations.end()) {
                    child.image_preview = views::render_effect_visual_view(r);
                } else {
                    const auto found = textures.find(animation->second.texture);
                    child.image_preview = views::render_sprite_view(
                        animation->second, r.id,
                        found != textures.end() ? &found->second : nullptr);
                    child.detail += "\nSprite: texture T" +
                                    std::to_string(animation->second.texture) + ", " +
                                    std::to_string(animation->second.frames.size()) +
                                    " frames, frame time " +
                                    std::to_string(animation->second.frame_time) +
                                    (animation->second.loop ? ", loop" : ", once");
                }
                child.info_preview = views::render_effect_record_view(r);
            } else {
                const ImagePreview* texture = nullptr;
                const effect_bank::SpriteAnimation* animation = nullptr;
                if (r.kind == 'E') {
                    if (const auto runtime = effect_bank::e_runtime_view(r)) {
                        const auto texture_found = textures.find(runtime->texture_id);
                        if (texture_found != textures.end()) {
                            texture = &texture_found->second;
                        }
                        if (runtime->uses_animation) {
                            const auto animation_found =
                                animations.find(runtime->animation_id);
                            if (animation_found != animations.end()) {
                                animation = &animation_found->second;
                            }
                        }
                        child.detail += "\nRuntime E: mode " +
                                        std::to_string(runtime->mode) +
                                        ", T " + std::to_string(runtime->texture_id);
                        if (runtime->uses_animation) {
                            child.detail += ", A " +
                                            std::to_string(runtime->animation_id);
                        }
                    }
                }

                child.image_preview =
                    views::render_effect_visual_view(r, texture, animation);
                child.info_preview =
                    views::render_effect_record_view(r, texture, animation);
            }

            if (child.image_preview.available()) {
                child.capabilities =
                    child.capabilities | ResourceCapability::ImagePreview;
            }
        }

        // V is a composite dispatcher. Re-render its Visual surface after every
        // direct child acquired its own Visual image so V can show the actual
        // resolved E texture imagery (and the graphical P/G/V child views)
        // instead of only labels. This is a static reconstruction of confirmed
        // dependencies/transforms, not yet a claim of frame-accurate gameplay.
        for (const auto& r : bank->records) {
            if (r.kind != 'V' || r.bytes.empty()) continue;
            const auto runtime = effect_bank::v_runtime_view(r);
            const auto child_found = child_by_key.find({r.kind, r.id});
            if (!runtime || child_found == child_by_key.end()) continue;

            std::vector<views::EffectLinkedVisual> linked;
            linked.reserve(runtime->entries.size());
            for (const auto& entry : runtime->entries) {
                char kind = '?';
                switch (entry.dispatch) {
                case 0U: kind = 'P'; break;
                case 1U: kind = 'E'; break;
                case 2U: kind = 'G'; break;
                case 3U: kind = 'V'; break;
                default: break;
                }

                const ImagePreview* preview = nullptr;
                if (kind != '?') {
                    const auto target = child_by_key.find({kind, entry.id});
                    if (target != child_by_key.end()) {
                        const auto& target_child = out.children[target->second];
                        if (target_child.image_preview.available()) {
                            preview = &target_child.image_preview;
                        }
                    }
                }
                linked.push_back({
                    kind,
                    entry.id,
                    entry.translation,
                    entry.rotation_degrees,
                    entry.scale,
                    preview,
                });
            }

            auto& child = out.children[child_found->second];
            child.image_preview = views::render_effect_visual_view(
                r, nullptr, nullptr, linked);
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
