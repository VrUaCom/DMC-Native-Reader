#include "reader_bridge.h"

#include <limits>
#include <utility>

#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_limits.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/view_renderer.h"

namespace dmc_ios {
namespace {

namespace bw = dmcresource::spider::black_widow;

// The shell-side constants must stay equal to the native definitions.
static_assert(state::kCanRender == bw::state_flag(bw::StateFlag::CanRender));
static_assert(state::kCanWireframe == bw::state_flag(bw::StateFlag::CanWireframe));
static_assert(state::kCanPreviewImage == bw::state_flag(bw::StateFlag::CanPreviewImage));
static_assert(state::kChildBrowserMode == bw::state_flag(bw::StateFlag::ChildBrowserMode));
static_assert(state::kTextureCompanionAttachable ==
              bw::state_flag(bw::StateFlag::TextureCompanionAttachable));
static_assert(state::kTextureCompanionAttached ==
              bw::state_flag(bw::StateFlag::TextureCompanionAttached));
static_assert(render_flag::kWireframe ==
              dmcresource::render_flag(dmcresource::RenderFlag::Wireframe));

bool copy_pixels(std::uint32_t width, std::uint32_t height,
                 const std::vector<std::uint8_t>& rgba, Pixels* out) {
    if (out == nullptr || width == 0U || height == 0U) return false;
    const auto expected = static_cast<std::uint64_t>(width) *
                          static_cast<std::uint64_t>(height) * 4U;
    if (expected != rgba.size()) return false;
    out->width = width;
    out->height = height;
    out->rgba = rgba;
    return true;
}

}  // namespace

ReaderSession::ReaderSession(std::unique_ptr<dmcresource::Session> session) noexcept
    : session_(std::move(session)) {}

ReaderSession::~ReaderSession() = default;

std::unique_ptr<ReaderSession> ReaderSession::open(
        std::string_view name, const std::uint8_t* bytes, std::size_t size) noexcept {
    if (bytes == nullptr && size != 0U) return nullptr;
    if (size > dmcresource::resource_limits::kMaxResourceBytes) return nullptr;
    try {
        auto session = dmcresource::open_session(name, bytes, size);
        if (!session) return nullptr;
        return std::unique_ptr<ReaderSession>(new ReaderSession(std::move(session)));
    } catch (...) {
        return nullptr;
    }
}

std::uint64_t ReaderSession::state() const noexcept {
    return dmcresource::black_widow_state(session_.get());
}

std::string ReaderSession::describe() const noexcept {
    try {
        return dmcresource::describe_session(session_.get());
    } catch (...) {
        return {};
    }
}

std::string ReaderSession::inspection() const noexcept {
    try {
        return dmcresource::format_inspection_tree(session_->inspection);
    } catch (...) {
        return {};
    }
}

bool ReaderSession::render(int width, int height, float yaw, float pitch,
                           float zoom, std::uint32_t flags,
                           Pixels* out) const noexcept {
    try {
        const auto image = dmcresource::render_session(
            session_.get(), width, height, yaw, pitch, zoom, flags);
        if (image.width <= 0 || image.height <= 0) return false;
        return copy_pixels(static_cast<std::uint32_t>(image.width),
                           static_cast<std::uint32_t>(image.height),
                           image.pixels, out);
    } catch (...) {
        return false;
    }
}

bool ReaderSession::image_preview(Pixels* out) const noexcept {
    try {
        const auto& preview = session_->image_preview;
        if (!preview.available()) return false;
        return copy_pixels(preview.width, preview.height, preview.rgba8, out);
    } catch (...) {
        return false;
    }
}

std::size_t ReaderSession::child_count() const noexcept {
    return dmcresource::session_child_count(session_.get());
}

std::string ReaderSession::child_title(int index) const noexcept {
    try {
        return dmcresource::session_child_title(session_.get(), index);
    } catch (...) {
        return {};
    }
}

bool ReaderSession::child_preview(int index, Pixels* out) const noexcept {
    try {
        dmcresource::ImagePreview scratch;
        const auto* preview =
            dmcresource::session_child_preview(session_.get(), index, &scratch);
        if (preview == nullptr || !preview->available()) return false;
        return copy_pixels(preview->width, preview->height, preview->rgba8, out);
    } catch (...) {
        return false;
    }
}

std::unique_ptr<ReaderSession> ReaderSession::open_child(int index) const noexcept {
    try {
        auto child = dmcresource::open_session_child(session_.get(), index);
        if (!child) return nullptr;
        return std::unique_ptr<ReaderSession>(new ReaderSession(std::move(child)));
    } catch (...) {
        return nullptr;
    }
}

bool ReaderSession::attach_texture(std::string_view name, const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    if (bytes == nullptr && size != 0U) return false;
    if (size > dmcresource::resource_limits::kMaxResourceBytes) return false;
    return dmcresource::spider::actions::attach_ptx(session_.get(), name, bytes, size);
}

std::string ReaderSession::texture_attachment_detail() const noexcept {
    try {
        return session_->texture_attachment_detail;
    } catch (...) {
        return {};
    }
}

}  // namespace dmc_ios
