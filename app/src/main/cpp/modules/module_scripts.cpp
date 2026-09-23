#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "dmcresource/module_support.h"
#include "dmcresource/motion/cloth_chain.h"
#include "dmcresource/motion/uv_scroll.h"

namespace dmcresource {
namespace {

[[nodiscard]] std::string pair_text(float a, float b) {
    std::ostringstream out;
    out << a << ", " << b;
    return out.str();
}

[[nodiscard]] const char* scroll_type_name(std::uint8_t type) noexcept {
    switch (type) {
    case 0U: return "0 stepped (RateUV per InterUV frames)";
    case 1U: return "1 linear (one texture per TimeUV frames)";
    case 2U: return "2 eased (RateUV, cosine ease)";
    case 3U: return "3 eased (one texture per TimeUV frames, cosine ease)";
    case 4U: return "4 (not ported)";
    case 5U: return "5 (not ported)";
    case 10U: return "10 (model flag 2, not ported)";
    default: return "unknown";
    }
}

[[nodiscard]] const char* u_direction(std::int16_t d) noexcept {
    return d > 0 ? "left" : d < 0 ? "right" : "stay";
}

[[nodiscard]] const char* v_direction(std::int16_t d) noexcept {
    return d > 0 ? "up" : d < 0 ? "down" : "stay";
}

[[nodiscard]] PipelineResult accepted(const NativeModule& module,
                                      const ProbeResult& probe,
                                      std::size_t size,
                                      const char* format,
                                      const char* parser,
                                      const char* title) {
    PipelineResult out;
    out.accepted = true;
    out.renderable = false;
    out.probe = probe;
    out.modules.push_back({"identity-probe", true});
    out.modules.push_back({"bounded-read-guard", true});
    out.modules.push_back({parser, true});
    out.modules.push_back({module.id, true});
    out.inspection.format = format;
    out.inspection.root.id = format;
    out.inspection.root.title = title;
    out.inspection.root.kind = InspectionKind::Document;
    out.inspection.root.source_span = SourceSpan{0U, size};
    return out;
}

// .tsc texture scroll script (CDrawUV parser 0x14030A9B0 / 0x14030ABE0).
PipelineResult run_tsc_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    try {
        const std::string_view text{reinterpret_cast<const char*>(bytes), size};
        const auto records = motion::parse_tsc(text);
        if (records.empty()) {
            return module_support::reject(probe, module.id, "TSC has no RELATIVE scroll block");
        }
        auto out = accepted(module, probe, size, "TSC", "canonical.tsc.parser",
                            "TSC texture scroll");
        out.inspection.root.properties.push_back(
            {"Scrolls", std::to_string(records.size()), EvidenceLevel::ExeConfirmed});
        out.inspection.root.properties.push_back(
            {"Binding", "MOD objects whose flags +0x10 bits 24-27 = ScrlNo + 1",
             EvidenceLevel::ExeConfirmed});
        out.inspection.root.properties.push_back(
            {"Playback", "open the owning PAC and play a motion to see the scroll",
             EvidenceLevel::Recognized});
        std::ostringstream detail;
        detail << "TSC texture scroll | scrolls=" << records.size();
        for (const auto& r : records) {
            InspectionNode node;
            node.id = "scroll-" + std::to_string(r.number);
            node.title = "Scroll " + std::to_string(r.number);
            node.kind = InspectionKind::Collection;
            const auto add = [&node](const char* name, std::string value) {
                node.properties.push_back({name, std::move(value), EvidenceLevel::ExeConfirmed});
            };
            add("ScrlType", scroll_type_name(r.type));
            add("TexNo", r.texture < 0 ? "all meshes" : std::to_string(r.texture));
            add("DirUV", std::string{u_direction(r.direction[0])} + ", " +
                             v_direction(r.direction[1]));
            add("TimeUV", pair_text(r.time[0], r.time[1]));
            add("RateUV", pair_text(r.rate[0], r.rate[1]));
            add("InterUV", pair_text(r.interval[0], r.interval[1]));
            if (r.has_minimum) add("MinimumUV", pair_text(r.minimum[0], r.minimum[1]));
            if (r.has_random) add("RndUV", "present (random jitter, not reproduced)");
            out.inspection.root.children.push_back(std::move(node));
            detail << "\n  scroll " << static_cast<unsigned>(r.number) << ": type "
                   << static_cast<unsigned>(r.type) << " tex " << r.texture << " dir "
                   << u_direction(r.direction[0]) << "/" << v_direction(r.direction[1])
                   << " time " << r.time[0] << "/" << r.time[1];
        }
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

// .clt chain/cloth parameters (parser 0x1402CA345 / 0x1402CA42A).
PipelineResult run_clt_module(const NativeModule& module,
                              std::string_view,
                              const std::uint8_t* bytes,
                              std::size_t size,
                              const ProbeResult& probe) noexcept {
    try {
        const std::string_view text{reinterpret_cast<const char*>(bytes), size};
        const auto blocks = motion::parse_clt(text);
        if (blocks.empty()) {
            return module_support::reject(probe, module.id, "CLT has no cloth block");
        }
        auto out = accepted(module, probe, size, "CLT", "canonical.clt.parser",
                            "CLT chain / cloth");
        out.inspection.root.properties.push_back(
            {"Blocks", std::to_string(blocks.size()), EvidenceLevel::ExeConfirmed});
        out.inspection.root.properties.push_back(
            {"Solver", "per-node step 0x1402C9450, one step per game frame",
             EvidenceLevel::ExeConfirmed});
        std::ostringstream detail;
        detail << "CLT chain/cloth | blocks=" << blocks.size();
        for (std::size_t index = 0U; index < blocks.size(); ++index) {
            const auto& p = blocks[index];
            InspectionNode node;
            node.id = "cloth-" + std::to_string(index);
            node.title = "Cloth " + std::to_string(index);
            node.kind = InspectionKind::Collection;
            const auto add = [&node](const char* name, std::string value) {
                node.properties.push_back({name, std::move(value), EvidenceLevel::ExeConfirmed});
            };
            std::ostringstream gravity;
            gravity << p.gravity[0] << ", " << p.gravity[1] << ", " << p.gravity[2];
            std::ostringstream wind;
            wind << p.wind[0] << ", " << p.wind[1] << ", " << p.wind[2];
            std::ostringstream bones;
            static constexpr const char* axes[] = {"X", "Y", "Z", "NX", "NY", "NZ"};
            for (std::size_t b = 0U; b < p.bones.size(); ++b) {
                if (b != 0U) bones << ", ";
                bones << p.bones[b].node << ' ' << axes[p.bones[b].axis < 6U ? p.bones[b].axis : 1U];
            }
            add("Gravity", gravity.str());
            add("Stiffness", std::to_string(p.stiffness));
            add("SpringForce", std::to_string(p.spring_force));
            add("MaxSpeed", std::to_string(p.max_speed));
            add("Wind", wind.str());
            add("WindLocal", p.wind_local ? "1" : "0");
            add("WindParent", std::to_string(p.wind_parent));
            add("LimitLength", p.limit_length ? "1" : "0");
            add("Bones", std::to_string(p.bones.size()) + ": " + bones.str());
            out.inspection.root.children.push_back(std::move(node));
            detail << "\n  cloth " << index << ": " << p.bones.size() << " bones, gravity "
                   << gravity.str() << ", stiffness " << p.stiffness;
        }
        out.detail = detail.str();
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule tsc_module() noexcept {
    return {"formats.tsc.scroll-reader", "TSC", Format::Tsc, ModuleKind::Structural, false,
            run_tsc_module, capability(ResourceCapability::Inspection)};
}

NativeModule clt_module() noexcept {
    return {"formats.clt.cloth-reader", "CLT", Format::Clt, ModuleKind::Structural, false,
            run_clt_module, capability(ResourceCapability::Inspection)};
}

}  // namespace dmcresource
