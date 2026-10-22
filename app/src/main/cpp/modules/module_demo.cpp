#include "dmcresource/native_module.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/demo.h"
#include "dmcresource/module_support.h"
#include "dmcresource/raster_card.h"

// Cutscenes: the archive (its script, clips and files, with the timeline as
// its picture), a cutscene camera (.cam: channels, keys, the path seen from
// above) and a clip text. The scene itself is assembled by demo_session.cpp.
namespace dmcresource {
namespace {

[[nodiscard]] std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", static_cast<double>(value));
    return text;
}

[[nodiscard]] std::string seconds(float frames) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f s", static_cast<double>(frames) / 60.0);
    return text;
}

void property(InspectionNode& node, std::string key, std::string value,
              EvidenceLevel evidence = EvidenceLevel::StructuralConfirmed) {
    node.properties.push_back({std::move(key), std::move(value), evidence});
}

[[nodiscard]] std::string timing(const demo::Clip& clip) {
    std::string text = number(clip.set_frame) + " .. " + number(clip.end_frame());
    if (clip.clip_scale != 1.0F) text += "  x" + number(clip.clip_scale);
    if (clip.skip_frame != 0.0F) text += "  skip " + number(clip.skip_frame);
    if (clip.cut_frame != 0.0F) text += "  cut " + number(clip.cut_frame);
    return text;
}

[[nodiscard]] std::string fields_text(const demo::Clip& clip) {
    std::string text;
    for (const auto& [key, values] : clip.fields) {
        if (!text.empty()) text += "  ";
        text += key;
        for (const auto& v : values) text += " " + v;
    }
    return text;
}

InspectionNode clips_node(const demo::ClipList& list) {
    InspectionNode node;
    node.id = list.file;
    node.title = list.kind + (list.track >= 0 ? " " + std::to_string(list.track) : std::string{}) + " - " +
                 std::to_string(list.clips.size()) + " clip(s)";
    node.kind = InspectionKind::Collection;
    for (std::size_t i = 0; i < list.clips.size() && i < 400U; ++i) {
        const auto& clip = list.clips[i];
        property(node, std::to_string(i) + "  " + timing(clip), fields_text(clip));
    }
    return node;
}

// The timeline: one row per clip list with clips, every clip a bar.
ImagePreview render_timeline(const demo::Document& doc) {
    std::vector<const demo::ClipList*> rows;
    for (const auto& list : doc.lists) {
        if (list.clips.empty()) continue;
        bool timed = false;
        for (const auto& clip : list.clips) timed = timed || clip.life < 100000.0F;
        if (timed) rows.push_back(&list);
    }
    const int row_h = 22, top = 96, left = 190, right = 1900;
    const int height = std::min(4000, top + static_cast<int>(rows.size()) * row_h + 40);
    raster::Canvas canvas(1920, std::max(height, 240));
    canvas.fill(0, 0, canvas.width(), canvas.height(), raster::kBackground);
    canvas.text(16, 12, "CUTSCENE " + doc.name + "  " + number(doc.end_frame) + " frames  " + seconds(doc.end_frame),
                raster::kLabel, 3);
    const float end = std::max(1.0F, doc.end_frame);
    const auto x_of = [&](float frame) {
        return left + static_cast<int>(std::clamp(frame / end, 0.0F, 1.0F) * static_cast<float>(right - left));
    };
    for (int s = 0; s <= static_cast<int>(end / 600.0F); ++s) {
        const int x = x_of(static_cast<float>(s) * 600.0F);
        canvas.line(x, top - 8, x, canvas.height() - 30, raster::kGrid);
        canvas.text(x + 3, top - 30, std::to_string(s * 10) + "s", raster::kDim, 2);
    }
    int y = top;
    for (const auto* list : rows) {
        if (y + row_h > canvas.height() - 20) break;
        const std::string label = list->kind + (list->track >= 0 ? " " + std::to_string(list->track) : std::string{});
        canvas.text(16, y + 3, label, raster::kLabel, 2);
        const raster::Rgb color = list->kind == "Camera"   ? raster::kAccent
                                  : list->kind == "Motion" ? raster::Rgb{110, 190, 250}
                                  : list->kind == "Hide"   ? raster::Rgb{170, 130, 230}
                                                           : raster::Rgb{120, 210, 150};
        for (const auto& clip : list->clips) {
            if (clip.life >= 100000.0F) continue;
            const int x0 = x_of(clip.set_frame), x1 = std::max(x0 + 2, x_of(clip.end_frame()));
            canvas.fill(x0, y + 3, x1, y + row_h - 3, color);
            canvas.line(x0, y + 2, x0, y + row_h - 2, raster::kBackground);
        }
        y += row_h;
    }
    return canvas.take();
}

// A camera seen from above: the eye path, the target path and a line from
// eye to target every few frames.
ImagePreview render_camera(const demo::CameraAnim& anim, std::string_view name) {
    raster::Canvas canvas(1080, 1080);
    canvas.fill(0, 0, 1080, 1080, raster::kBackground);
    canvas.text(16, 12, "CAMERA " + std::string{name} + "  frames " + number(anim.start) + " .. " + number(anim.end),
                raster::kLabel, 3);
    const float span = std::max(1.0F, anim.end - anim.start);
    const int steps = std::clamp(static_cast<int>(span), 2, 2000);
    std::vector<demo::CameraPose> poses;
    poses.reserve(static_cast<std::size_t>(steps) + 1U);
    float min_x = 1.0e30F, max_x = -1.0e30F, min_z = 1.0e30F, max_z = -1.0e30F;
    for (int i = 0; i <= steps; ++i) {
        const auto pose = demo::evaluate(anim, anim.start + span * static_cast<float>(i) / static_cast<float>(steps));
        for (const auto& p : {pose.eye, pose.target}) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_z = std::min(min_z, p.z);
            max_z = std::max(max_z, p.z);
        }
        poses.push_back(pose);
    }
    const float extent = std::max({max_x - min_x, max_z - min_z, 1.0F});
    const float scale = 900.0F / extent;
    const auto px = [&](const Vec3& p) {
        return std::pair<int, int>{90 + static_cast<int>((p.x - min_x) * scale),
                                   990 - static_cast<int>((p.z - min_z) * scale)};
    };
    for (std::size_t i = 0; i < poses.size(); i += std::max<std::size_t>(1U, poses.size() / 24U)) {
        const auto [ex, ey] = px(poses[i].eye);
        const auto [tx, ty] = px(poses[i].target);
        canvas.line(ex, ey, tx, ty, raster::kGrid);
    }
    for (std::size_t i = 1; i < poses.size(); ++i) {
        const auto [ax, ay] = px(poses[i - 1].eye);
        const auto [bx, by] = px(poses[i].eye);
        canvas.line(ax, ay, bx, by, raster::kAccent);
        const auto [cx, cy] = px(poses[i - 1].target);
        const auto [dx, dy] = px(poses[i].target);
        canvas.line(cx, cy, dx, dy, raster::Rgb{110, 190, 250});
    }
    const auto [sx, sy] = px(poses.front().eye);
    canvas.circle(sx, sy, 6, raster::kAccent);
    canvas.text(16, 1040, "EYE (from the circle)", raster::kAccent, 2);
    canvas.text(330, 1040, "TARGET", raster::Rgb{110, 190, 250}, 2);
    canvas.text(500, 1040, "TOP VIEW (X right, Z up)", raster::kDim, 2);
    return canvas.take();
}

constexpr const char* kChannelNames[demo::kCamChannels] = {
    "group x", "group y", "group z", "group rot x", "group rot y", "group rot z", "eye x",
    "eye y",   "eye z",   "target x", "target y",   "target z",    "fov (deg)",   "roll (deg)"};

PipelineResult run_demo(const NativeModule& module, std::string_view filename, const std::uint8_t* bytes,
                        std::size_t size, const ProbeResult& probe) noexcept {
    try {
        const std::span<const std::uint8_t> data{bytes, size};
        switch (module.format) {
        case Format::Demo: {
            auto doc = demo::parse_demo(data);
            if (!doc) return module_support::reject(probe, module.id, "not a cutscene archive");
            // The files stay browsable as the PAC's children.
            PipelineResult out;
            if (const auto* pac = NativeModuleRegistry::find("PAC")) {
                auto pac_probe = probe;
                pac_probe.format = Format::Pac;
                out = pac->run(*pac, filename, bytes, size, pac_probe);
            }
            if (!out.accepted) out = structural_pipeline(probe, module.id, "");
            out.probe = probe;
            out.capabilities = out.capabilities | module.capabilities;
            out.modules.push_back({module.id, true});
            auto& root = out.inspection.root;
            out.inspection.format = "DEMO";
            root = {};
            root.id = "demo";
            root.title = "Cutscene " + doc->name;
            root.kind = InspectionKind::Document;
            property(root, "Length", number(doc->end_frame) + " frames, " + seconds(doc->end_frame) + " at 60 fps");
            const auto tracks = doc->tracks();
            property(root, "Tracks", std::to_string(tracks.size()));
            property(root, "Cameras", std::to_string(doc->cameras.size()));
            property(root, "Files", std::to_string(doc->members.size()));
            if (!doc->skipped.empty()) {
                std::string skipped;
                for (const auto& s : doc->skipped) skipped += (skipped.empty() ? "" : ", ") + s;
                property(root, "Commented out", skipped);
            }
            InspectionNode track_list;
            track_list.id = "tracks";
            track_list.title = "Tracks";
            track_list.kind = InspectionKind::Collection;
            for (const auto& t : tracks) {
                std::string line;
                if (t.model != nullptr && !t.model->clips.empty()) {
                    const auto& c = t.model->clips.front();
                    line = c.value("Model");
                    if (const auto tex = c.value("Texture"); !tex.empty()) line += " + " + tex;
                    for (const auto& [key, values] : c.fields) {
                        if (key == "CnsPos" && values.size() >= 2U) {
                            line += "  on track " + values[0] + " joint " + values[1];
                        }
                    }
                }
                if (t.motion != nullptr) line += "  " + std::to_string(t.motion->clips.size()) + " motion clip(s)";
                if (t.hide != nullptr) line += "  " + std::to_string(t.hide->clips.size()) + " hide clip(s)";
                property(track_list, "Track " + std::to_string(t.index), line);
            }
            root.children.push_back(std::move(track_list));
            for (const auto& list : doc->lists) {
                if (!list.clips.empty()) root.children.push_back(clips_node(list));
            }
            out.image_preview = render_timeline(*doc);
            out.capabilities = out.capabilities | ResourceCapability::ImagePreview;
            out.detail = "DEMO | cutscene " + doc->name + ", " + std::to_string(tracks.size()) + " tracks, " +
                         std::to_string(doc->cameras.size()) + " cameras, " + seconds(doc->end_frame);
            return out;
        }
        case Format::DemoCamera: {
            const auto anim = demo::parse_cam(data);
            if (!anim) return module_support::reject(probe, module.id, "not a cutscene camera");
            auto out = structural_pipeline(probe, module.id, "");
            out.capabilities = module.capabilities;
            out.inspection.format = "DEMO-CAM";
            auto& root = out.inspection.root;
            root.id = "cam";
            root.title = "Cutscene camera";
            root.kind = InspectionKind::Document;
            property(root, "Frames", number(anim->start) + " .. " + number(anim->end));
            char mask[16];
            std::snprintf(mask, sizeof mask, "0x%X", anim->mask);
            property(root, "Animated channels", mask);
            InspectionNode base;
            base.id = "base";
            base.title = "Base values";
            base.kind = InspectionKind::Collection;
            for (std::size_t i = 0; i < demo::kCamChannels; ++i) property(base, kChannelNames[i], number(anim->base[i]));
            root.children.push_back(std::move(base));
            for (const auto& track : anim->tracks) {
                InspectionNode node;
                node.id = kChannelNames[static_cast<std::size_t>(track.channel)];
                node.title = std::string{"Track "} + kChannelNames[static_cast<std::size_t>(track.channel)] + " - " +
                             std::to_string(track.keys.size()) + (track.hermite ? " Hermite keys" : " linear keys");
                node.kind = InspectionKind::Collection;
                for (std::size_t k = 0; k < track.keys.size() && k < 300U; ++k) {
                    const auto& key = track.keys[k];
                    property(node, "frame " + number(key.frame),
                             track.hermite ? number(key.value) + "  slopes " + number(key.in_slope) + " / " +
                                                 number(key.out_slope)
                                           : number(key.value));
                }
                root.children.push_back(std::move(node));
            }
            out.image_preview = render_camera(*anim, filename);
            const auto first = demo::evaluate(*anim, anim->start);
            out.detail = "DEMO-CAM | frames " + number(anim->start) + ".." + number(anim->end) + ", " +
                         std::to_string(anim->tracks.size()) + " tracks, fov " + number(first.fov_degrees);
            return out;
        }
        case Format::DemoClips: {
            const std::string_view text{reinterpret_cast<const char*>(bytes), size};
            const auto clips = demo::parse_clips(text);
            auto out = structural_pipeline(probe, module.id, "");
            out.capabilities = module.capabilities;
            out.inspection.format = "CLIPS";
            auto& root = out.inspection.root;
            root.id = "clips";
            root.title = "Cutscene clips";
            root.kind = InspectionKind::Document;
            demo::ClipList list;
            list.kind = "Clips";
            list.clips = clips;
            property(root, "Clips", std::to_string(clips.size()));
            root.children.push_back(clips_node(list));
            out.detail = "CLIPS | " + std::to_string(clips.size()) + " clip(s)";
            return out;
        }
        default: break;
        }
        return module_support::reject(probe, module.id, "not a cutscene resource");
    } catch (...) {
        return module_support::reject(probe, module.id, "cutscene module failed");
    }
}

}  // namespace

std::vector<NativeModule> demo_modules() noexcept {
    const auto info = capability(ResourceCapability::Inspection);
    return {
        {"formats.demo.cutscene", "DEMO", Format::Demo, ModuleKind::Structural, false, run_demo,
         info | ResourceCapability::ImagePreview | ResourceCapability::ChildResources | ResourceCapability::Container},
        {"formats.demo.camera", "DEMO-CAM", Format::DemoCamera, ModuleKind::Structural, false, run_demo,
         info | ResourceCapability::ImagePreview},
        {"formats.demo.clips", "CLIPS", Format::DemoClips, ModuleKind::Structural, false, run_demo, info},
    };
}

}  // namespace dmcresource
