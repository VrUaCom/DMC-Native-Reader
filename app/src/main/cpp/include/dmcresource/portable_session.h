#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/view_renderer.h"

namespace dmcresource {

// Platform-neutral owner for one decoded Architecture v2 resource.
// Android, iOS and Windows shells can consume this object without owning
// format-specific parsers. All binary semantics stay in the same C++20 core.
class PortableSession final {
public:
    static std::unique_ptr<PortableSession> decode(std::string_view filename,
                                                   const std::uint8_t* bytes,
                                                   std::size_t size,
                                                   std::string* error = nullptr);

    PortableSession(const PortableSession&) = delete;
    PortableSession& operator=(const PortableSession&) = delete;
    PortableSession(PortableSession&&) = delete;
    PortableSession& operator=(PortableSession&&) = delete;
    ~PortableSession() = default;

    [[nodiscard]] const PipelineResult& result() const noexcept { return result_; }
    [[nodiscard]] const Mesh& render_mesh() const noexcept { return render_mesh_; }
    [[nodiscard]] const HierarchyOverlay& hierarchy() const noexcept { return hierarchy_; }
    [[nodiscard]] bool has_geometry() const noexcept { return geometry_ready_; }
    [[nodiscard]] bool hierarchy_available() const noexcept {
        return has_capability(result_.capabilities, ResourceCapability::NodeHierarchy) &&
               hierarchy_.available();
    }
    [[nodiscard]] bool has_image_preview() const noexcept {
        return result_.image_preview.available();
    }
    [[nodiscard]] std::size_t child_count() const noexcept { return result_.children.size(); }
    [[nodiscard]] const ChildResource* child(std::size_t index) const noexcept;

    // Generic render flags are shared across platform shells. The core decides
    // whether a requested overlay is actually evidence/capability-authorized.
    [[nodiscard]] RgbaImage render(int width,
                                   int height,
                                   const ViewState& view,
                                   RenderFlags flags = 0U) const;

    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string inspection_text(std::size_t max_lines = 4096U) const;

private:
    explicit PortableSession(PipelineResult&& result);

    PipelineResult result_;
    Mesh render_mesh_;
    HierarchyOverlay hierarchy_;
    bool geometry_ready_{false};
};

}  // namespace dmcresource
