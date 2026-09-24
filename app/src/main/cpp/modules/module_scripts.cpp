#include "dmcresource/native_module.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <span>
#include <utility>

#include "dmcresource/collision_shapes.h"
#include "dmcresource/format_views.h"
#include "dmcresource/module_support.h"
#include "dmcresource/motion/motion_script.h"
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
    case 4U: return "4 ping-pong (turns after TurnTimeUV)";
    case 5U: return "5 ping-pong, cosine ease";
    case 10U: return "10 facing (JntNo Z axis vs view)";
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
        out.image_preview = views::render_tsc_view(records);
        out.inspection.root.properties.push_back(
            {"View", "U/V offset per scroll over 240 frames + scrolled checker",
             EvidenceLevel::ExeConfirmed});
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
        out.image_preview = views::render_clt_view(blocks);
        out.inspection.root.properties.push_back(
            {"View", "bone chains, gravity/wind arrows, solver parameters",
             EvidenceLevel::ExeConfirmed});
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

// Player motion script (pl000.pac slot 5; loader 0x1400594B0, interpreter
// 0x140058FE0).
PipelineResult run_motion_script_module(const NativeModule& module,
                                        std::string_view,
                                        const std::uint8_t* bytes,
                                        std::size_t size,
                                        const ProbeResult& probe) noexcept {
    try {
        const auto file = motion::MotionScriptFile::parse(std::span<const std::uint8_t>{bytes, size});
        if (!file) {
            return module_support::reject(probe, module.id, "motion script bank tables do not parse");
        }
        auto out = accepted(module, probe, size, "MotionScript", "canonical.motion-script.parser",
                            file->nested() ? "Player motion script" : "Enemy motion script");
        const auto add_root = [&out](const char* key, std::string value) {
            out.inspection.root.properties.push_back({key, std::move(value), EvidenceLevel::ExeConfirmed});
        };
        std::size_t total = 0U;
        for (std::size_t bank = 0U; bank < file->bank_count(); ++bank) total += file->script_count(bank);
        add_root("Header table", "+" + std::to_string(file->header_table()));
        add_root("Form", file->nested()
                             ? "player (bind mode 0: banks = pl000_00_N.pac)"
                             : "enemy (bind mode 1: actions of the actor's motion PACs)");
        add_root("Motion resources", file->has_resources()
                                         ? "table B: action -> MOT id (group * 100 + slot), loop flag"
                                         : "none");
        add_root("Banks", std::to_string(file->bank_count()));
        add_root("Scripts", std::to_string(total));
        add_root("Weapon states", "opcode 3 byte 2 & 0x3F -> player+0x39C3 (0x1401F01F0)");
        std::ostringstream detail;
        detail << "Player motion script | banks=" << file->bank_count() << " scripts=" << total;
        for (std::size_t bank = 0U; bank < file->bank_count(); ++bank) {
            InspectionNode node;
            node.id = "bank-" + std::to_string(bank);
            const auto count = file->script_count(bank);
            node.title = file->nested()
                ? "Bank " + std::to_string(bank) + " (pl000_00_" + std::to_string(bank) + ") " +
                      std::to_string(count) + " scripts"
                : "Bank " + std::to_string(bank) + " " + std::to_string(count) + " actions";
            node.kind = InspectionKind::Collection;
            for (std::size_t i = 0U; i < count && i < 128U; ++i) {
                const auto s = file->summarize(bank, i);
                if (!s) continue;
                std::ostringstream v;
                v << "MOT " << static_cast<unsigned>(s->play_bank) << "/"
                  << static_cast<unsigned>(s->play_index) << ", " << s->instructions << " ops, "
                  << s->waits << " waits";
                if (s->last_frame != 0U) v << " to F" << s->last_frame;
                if (s->loops) v << ", loop";
                if (s->hands_over) v << ", hands over";
                for (const auto& r : file->resources(bank, i)) {
                    if (r.object != 0U) continue;
                    v << ", plays MOT " << r.id << " (PAC group " << r.group() << " slot "
                      << r.slot() << (r.loop == 1U ? ", loop" : r.loop == 2U ? ", actor loop" : "")
                      << ")";
                }
                if (!s->states.empty()) {
                    v << ", states";
                    for (const auto& key : s->states) {
                        v << ' ' << static_cast<unsigned>(key.state);
                        if (key.after_frame >= 0.0F) v << "@" << key.after_frame;
                    }
                }
                node.properties.push_back({"Script " + std::to_string(i), v.str(),
                                           EvidenceLevel::ExeConfirmed});
            }
            out.inspection.root.children.push_back(std::move(node));
            detail << "\n  bank " << bank << ": " << count << " scripts";
        }
        out.detail = detail.str();
        out.image_preview = views::render_motion_script_view(*file);
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

// Collision shape table (ICollisionHandle records, 0x14005C260 / 0x1402CC115).
PipelineResult run_colshape_module(const NativeModule& module,
                                   std::string_view,
                                   const std::uint8_t* bytes,
                                   std::size_t size,
                                   const ProbeResult& probe) noexcept {
    try {
        const std::span<const std::uint8_t> data{bytes, size};
        if (!collision::looks_like_shape_table(data)) {
            return module_support::reject(probe, module.id, "not whole 80-byte shape records");
        }
        const auto shapes = collision::parse_shapes(data);
        auto out = accepted(module, probe, size, "COLSHAPE", "canonical.collision.shape-reader",
                            "Collision shapes");
        out.inspection.root.properties.push_back(
            {"Records", std::to_string(shapes.size()) + " x 80 bytes", EvidenceLevel::ExeConfirmed});
        out.inspection.root.properties.push_back(
            {"Placement", "bone space; the attack index names the bone", EvidenceLevel::ExeConfirmed});
        std::ostringstream detail;
        detail << "Collision shapes | records=" << shapes.size();
        for (std::size_t i = 0U; i < shapes.size() && i < 512U; ++i) {
            const auto& s = shapes[i];
            InspectionNode node;
            node.id = "shape-" + std::to_string(i);
            node.kind = InspectionKind::Object;
            std::ostringstream v;
            v.precision(4);
            switch (s.type) {
            case 2U:
                node.title = "#" + std::to_string(i) + " sphere";
                v << "centre " << s.a[0] << ", " << s.a[1] << ", " << s.a[2] << "  radius " << s.radius;
                break;
            case 3U:
                node.title = "#" + std::to_string(i) + " box";
                v << "centre " << s.a[0] << ", " << s.a[1] << ", " << s.a[2] << "  rotation " << s.b[0]
                  << ", " << s.b[1] << ", " << s.b[2] << " deg  half size " << s.size[0] << ", " << s.size[1]
                  << ", " << s.size[2];
                break;
            case 4U:
                node.title = "#" + std::to_string(i) + " capsule";
                v << "a " << s.a[0] << ", " << s.a[1] << ", " << s.a[2] << "  b " << s.b[0] << ", " << s.b[1]
                  << ", " << s.b[2] << "  radius " << s.radius;
                break;
            default:
                node.title = "#" + std::to_string(i) + " type " + std::to_string(s.type);
                v << "not drawn (type " << static_cast<unsigned>(s.type) << ")";
                break;
            }
            node.properties.push_back({"Shape", v.str(),
                                       s.type >= 2U && s.type <= 4U ? EvidenceLevel::ExeConfirmed
                                                                    : EvidenceLevel::Recognized});
            out.inspection.root.children.push_back(std::move(node));
        }
        out.detail = detail.str();
        out.image_preview = views::render_collision_view(shapes);
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

// Attack index (0x14005C740: entry = mask, bone, u16 shape).
PipelineResult run_colindex_module(const NativeModule& module,
                                   std::string_view,
                                   const std::uint8_t* bytes,
                                   std::size_t size,
                                   const ProbeResult& probe) noexcept {
    try {
        const std::span<const std::uint8_t> data{bytes, size};
        if (!collision::looks_like_attack_index(data, std::nullopt)) {
            return module_support::reject(probe, module.id, "not 4-byte attack entries");
        }
        const auto entries = collision::parse_attack_index(data);
        auto out = accepted(module, probe, size, "COLINDEX", "canonical.collision.index-reader",
                            "Attack collision index");
        std::size_t used = 0U;
        std::ostringstream list;
        for (std::size_t i = 0U; i < entries.size(); ++i) {
            const auto& e = entries[i];
            if (e.mask == 0U) continue;
            if (used < 400U) {
                list << (used == 0U ? "" : "; ") << i << ": mask " << static_cast<unsigned>(e.mask) << " bone "
                     << static_cast<unsigned>(e.bone) << " shape " << e.shape;
            }
            ++used;
        }
        out.inspection.root.properties.push_back(
            {"Attack ids", std::to_string(entries.size()) + " (" + std::to_string(used) + " used)",
             EvidenceLevel::ExeConfirmed});
        out.inspection.root.properties.push_back(
            {"Entries", list.str(), EvidenceLevel::ExeConfirmed});
        out.detail = "Attack collision index | ids=" + std::to_string(entries.size()) +
                     " used=" + std::to_string(used);
        out.image_preview = views::render_attack_index_view(entries);
        return out;
    } catch (...) {
        return module_support::reject_minimal(probe);
    }
}

}  // namespace

NativeModule colshape_module() noexcept {
    return {"formats.collision.shape-reader", "COLSHAPE", Format::CollisionShapes,
            ModuleKind::Structural, false, run_colshape_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

NativeModule colindex_module() noexcept {
    return {"formats.collision.index-reader", "COLINDEX", Format::AttackIndex,
            ModuleKind::Structural, false, run_colindex_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

NativeModule tsc_module() noexcept {
    return {"formats.tsc.scroll-reader", "TSC", Format::Tsc, ModuleKind::Structural, false,
            run_tsc_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

NativeModule clt_module() noexcept {
    return {"formats.clt.cloth-reader", "CLT", Format::Clt, ModuleKind::Structural, false,
            run_clt_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

NativeModule motion_script_module() noexcept {
    return {"formats.motion-script.reader", "MotionScript", Format::MotionScript,
            ModuleKind::Structural, false, run_motion_script_module,
            capability(ResourceCapability::Inspection) | ResourceCapability::ImagePreview};
}

}  // namespace dmcresource
