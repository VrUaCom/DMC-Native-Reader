#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/view_renderer.h"

namespace {

constexpr std::size_t kMaxMappedBytes = 512u * 1024u * 1024u;
constexpr std::uint32_t kMaxCompanionTextureSlot = 4095U;

std::string to_utf8(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* raw = env->GetStringUTFChars(value, nullptr);
    if (raw == nullptr) return {};
    std::string out(raw);
    env->ReleaseStringUTFChars(value, raw);
    return out;
}

class ReadOnlyMap {
public:
    explicit ReadOnlyMap(int fd) {
        struct stat st {};
        if (fd < 0 || fstat(fd, &st) != 0 || st.st_size < 0) return;
        const auto size64 = static_cast<std::uint64_t>(st.st_size);
        if (size64 > kMaxMappedBytes) return;
        size_ = static_cast<std::size_t>(size64);
        if (size_ == 0) {
            valid_ = true;
            return;
        }
        void* p = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            size_ = 0;
            return;
        }
        data_ = static_cast<const std::uint8_t*>(p);
        valid_ = true;
    }

    ~ReadOnlyMap() {
        if (data_ != nullptr && size_ != 0) {
            munmap(const_cast<std::uint8_t*>(data_), size_);
        }
    }

    bool valid() const noexcept { return valid_; }
    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    bool valid_{};
};

struct Session {
    dmcresource::ProbeResult probe;

    // Architecture v2 reusable session state. The same contracts are used for
    // top-level resources and children opened from a container/browser.
    dmcresource::ResourceCapabilities capabilities{};
    dmcresource::InspectionDocument inspection;
    dmcresource::RenderScene scene;
    dmcresource::ImagePreview image_preview;
    std::vector<dmcresource::ChildResource> children;

    // Static viewer caches. World-space geometry and hierarchy positions are
    // materialized once per session, never once per touch/rotation frame.
    dmcresource::Mesh render_mesh;
    dmcresource::HierarchyOverlay hierarchy_overlay;

    // Companion textures stay frontend-neutral. Vector index == canonical
    // texture slot; unavailable entries remain empty. PTX ownership/parsing is
    // intentionally outside the renderer and Java UI.
    std::vector<dmcresource::ImagePreview> attached_textures;
    std::string texture_attachment_detail;

    std::string detail;
    std::string trace;
    bool renderable{};
};

Session* from_handle(jlong handle) noexcept {
    return reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
}

jlong to_handle(Session* session) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
}

const dmcresource::ChildResource* child_at(const Session* session, jint index) noexcept {
    if (session == nullptr || index < 0) return nullptr;
    const auto i = static_cast<std::size_t>(index);
    return i < session->children.size() ? &session->children[i] : nullptr;
}

void prepare_session_caches(Session* session) {
    if (session == nullptr) return;

    if (!dmcresource::materialize_hierarchy_overlay(
            session->scene, &session->hierarchy_overlay)) {
        session->hierarchy_overlay = {};
        if (!session->detail.empty()) session->detail += "\n";
        session->detail += "Hierarchy overlay rejected malformed node/matrix data";
    }

    if (session->renderable) {
        if (!session->scene.has_geometry() ||
            !dmcresource::materialize_render_scene(session->scene,
                                                   &session->render_mesh)) {
            session->renderable = false;
            if (!session->detail.empty()) session->detail += "\n";
            session->detail +=
                "RenderScene projection rejected malformed or missing geometry";
        }
    }
}

std::unique_ptr<Session> session_from_child(const dmcresource::ChildResource& child) {
    auto session = std::make_unique<Session>();
    session->probe = child.probe;
    session->capabilities = child.capabilities;
    session->inspection = child.inspection;
    session->scene = child.scene;
    session->image_preview = child.image_preview;
    session->children = child.children;
    session->detail = child.detail;
    session->trace = child.trace;
    session->renderable = child.renderable;
    prepare_session_caches(session.get());
    return session;
}

jintArray rgba_to_argb(JNIEnv* env, std::size_t width, std::size_t height,
                       const std::vector<std::uint8_t>& rgba) {
    if (width == 0U || height == 0U) return nullptr;
    if (width > std::numeric_limits<std::size_t>::max() / height) return nullptr;
    const std::size_t count64 = width * height;
    if (count64 > static_cast<std::size_t>(std::numeric_limits<jsize>::max()) ||
        count64 > std::numeric_limits<std::size_t>::max() / 4U ||
        rgba.size() != count64 * 4U) {
        return nullptr;
    }

    const jsize count = static_cast<jsize>(count64);
    jintArray result = env->NewIntArray(count);
    if (result == nullptr) return nullptr;

    std::vector<jint> argb;
    try {
        argb.resize(count64);
    } catch (...) {
        return nullptr;
    }
    for (std::size_t i = 0; i < count64; ++i) {
        const std::size_t o = i * 4u;
        const std::uint32_t r = rgba[o + 0U];
        const std::uint32_t g = rgba[o + 1U];
        const std::uint32_t b = rgba[o + 2U];
        const std::uint32_t a = rgba[o + 3U];
        argb[i] = static_cast<jint>((a << 24U) | (r << 16U) | (g << 8U) | b);
    }
    env->SetIntArrayRegion(result, 0, count, argb.data());
    return result;
}

jintArray image_to_argb(JNIEnv* env, const dmcresource::RgbaImage& image) {
    if (image.width <= 0 || image.height <= 0) return nullptr;
    return rgba_to_argb(env,
                        static_cast<std::size_t>(image.width),
                        static_cast<std::size_t>(image.height),
                        image.pixels);
}

jintArray preview_to_argb(JNIEnv* env, const dmcresource::ImagePreview& image) {
    if (!image.available()) return nullptr;
    return rgba_to_argb(env,
                        static_cast<std::size_t>(image.width),
                        static_cast<std::size_t>(image.height),
                        image.rgba8);
}

[[nodiscard]] bool collect_required_texture_slots(
        const Session& session,
        std::vector<std::uint32_t>* required,
        std::uint32_t* max_slot) {
    if (required == nullptr || max_slot == nullptr) return false;
    required->clear();
    *max_slot = 0U;
    if (!session.renderable || !session.render_mesh.has_uv0() ||
        !session.render_mesh.has_triangle_texture_slots()) {
        return false;
    }

    try {
        for (const auto slot : session.render_mesh.triangle_texture_slots) {
            if (slot == dmcresource::Mesh::kNoTextureSlot) continue;
            if (slot > kMaxCompanionTextureSlot) return false;
            if (std::find(required->begin(), required->end(), slot) == required->end()) {
                required->push_back(slot);
                *max_slot = std::max(*max_slot, slot);
            }
        }
    } catch (...) {
        return false;
    }
    return !required->empty();
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_open(
        JNIEnv* env, jclass, jint fd, jstring filename) {
    if (fd < 0) return 0;
    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) return 0;

    const auto name = to_utf8(env, filename);
    auto pipeline = dmcresource::run_decode_pipeline(name, mapped.data(), mapped.size());
    if (!pipeline.accepted) return 0;

    try {
        auto session = std::make_unique<Session>();
        session->probe = pipeline.probe;
        session->capabilities = pipeline.capabilities;
        session->inspection = std::move(pipeline.inspection);
        session->scene = std::move(pipeline.scene);
        session->image_preview = std::move(pipeline.image_preview);
        session->children = std::move(pipeline.children);
        session->detail = std::move(pipeline.detail);
        session->trace = dmcresource::pipeline_trace(pipeline);
        session->renderable = pipeline.renderable;
        prepare_session_caches(session.get());
        return to_handle(session.release());
    } catch (...) {
        return 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_close(
        JNIEnv*, jclass, jlong handle) {
    delete from_handle(handle);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_info(
        JNIEnv* env, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("no session");

    std::ostringstream out;
    out << session->probe.family
        << " | domain=" << session->probe.domain
        << " | support=" << session->probe.support
        << " | evidence=" << session->probe.evidence
        << " | identity="
        << (session->probe.content_confirmed ? "content-confirmed" : "extension/name-only");
    if (session->renderable) {
        out << " | vertices=" << session->render_mesh.vertices.size()
            << " | triangles=" << (session->render_mesh.indices.size() / 3u)
            << " | uv0=" << session->render_mesh.uv0.size();
    } else if (session->image_preview.available()) {
        out << " | imagePreview=" << session->image_preview.width
            << "x" << session->image_preview.height;
    } else {
        out << " | preview=inspection";
    }
    if (!session->children.empty()) {
        out << " | children=" << session->children.size();
    }
    out << " | spatialHierarchy="
        << (session->hierarchy_overlay.available() ? "yes" : "no");
    if (!session->attached_textures.empty()) {
        std::size_t attached = 0U;
        for (const auto& texture : session->attached_textures) {
            if (texture.available()) ++attached;
        }
        out << " | companionTextures=" << attached;
    }
    if (!session->detail.empty()) out << "\n" << session->detail;
    if (!session->texture_attachment_detail.empty()) {
        out << "\n" << session->texture_attachment_detail;
    }
    if (!session->trace.empty()) out << "\n" << session->trace;
    return env->NewStringUTF(out.str().c_str());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_capabilities(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return 0;
    return static_cast<jlong>(session->capabilities);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hierarchyAvailable(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    return session->hierarchy_overlay.available() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreviewAvailable(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    return session->image_preview.available() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreviewWidth(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr || !session->image_preview.available() ||
        session->image_preview.width > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(session->image_preview.width);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreviewHeight(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr || !session->image_preview.available() ||
        session->image_preview.height > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(session->image_preview.height);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreview(
        JNIEnv* env, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return nullptr;
    return preview_to_argb(env, session->image_preview);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachPtx(
        JNIEnv* env, jclass, jlong handle, jint fd, jstring filename) {
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return JNI_FALSE;

    session->texture_attachment_detail.clear();
    std::vector<std::uint32_t> required_slots;
    std::uint32_t max_slot = 0U;
    if (!collect_required_texture_slots(*session, &required_slots, &max_slot)) {
        session->texture_attachment_detail =
            "PTX companion rejected: current resource has no complete UV + texture-slot render mapping";
        return JNI_FALSE;
    }

    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) {
        session->texture_attachment_detail =
            "PTX companion rejected: could not map selected file";
        return JNI_FALSE;
    }

    const auto name = to_utf8(env, filename);
    auto pipeline = dmcresource::run_decode_pipeline(name, mapped.data(), mapped.size());
    if (!pipeline.accepted || pipeline.probe.format != dmcresource::Format::Ptx) {
        session->texture_attachment_detail =
            "PTX companion rejected: selected file did not pass the Native Reader PTX pipeline";
        return JNI_FALSE;
    }

    try {
        std::vector<dmcresource::ImagePreview> textures(
            static_cast<std::size_t>(max_slot) + 1U);
        for (const auto slot : required_slots) {
            const auto index = static_cast<std::size_t>(slot);
            if (index >= pipeline.children.size()) {
                std::ostringstream detail;
                detail << "PTX companion rejected: model requests texture slot "
                       << slot << " but PTX exposes only "
                       << pipeline.children.size() << " texture entries";
                session->texture_attachment_detail = detail.str();
                return JNI_FALSE;
            }
            auto& child = pipeline.children[index];
            if (!child.image_preview.available()) {
                std::ostringstream detail;
                detail << "PTX companion rejected: texture slot " << slot
                       << " has no decoded base-mip image";
                if (!child.detail.empty()) detail << " | " << child.detail;
                session->texture_attachment_detail = detail.str();
                return JNI_FALSE;
            }
            textures[index] = std::move(child.image_preview);
        }

        session->attached_textures = std::move(textures);
        std::ostringstream detail;
        detail << "PTX companion attached: " << name
               << " | requiredSlots=" << required_slots.size()
               << " | bundleTextures=" << pipeline.children.size()
               << " | route=Spider/PTX->DDS->UV";
        session->texture_attachment_detail = detail.str();
        return JNI_TRUE;
    } catch (...) {
        session->texture_attachment_detail =
            "PTX companion rejected: texture attachment allocation failed";
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_textureAttachmentInfo(
        JNIEnv* env, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    return env->NewStringUTF(session->texture_attachment_detail.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceCount(
        JNIEnv*, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr ||
        session->children.size() > static_cast<std::size_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(session->children.size());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceTitle(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    return env->NewStringUTF(child == nullptr ? "" : child->title.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewAvailable(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    return child != nullptr && child->image_preview.available() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewWidth(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    if (child == nullptr || !child->image_preview.available() ||
        child->image_preview.width > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(child->image_preview.width);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewHeight(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    if (child == nullptr || !child->image_preview.available() ||
        child->image_preview.height > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(child->image_preview.height);
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreview(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    if (child == nullptr) return nullptr;
    return preview_to_argb(env, child->image_preview);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openChild(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto* child = child_at(from_handle(handle), index);
    if (child == nullptr) return 0;
    try {
        auto session = session_from_child(*child);
        return to_handle(session.release());
    } catch (...) {
        return 0;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_inspection(
        JNIEnv* env, jclass, jlong handle) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    const auto text = dmcresource::format_inspection_tree(session->inspection);
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_render(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags) {
    const Session* session = from_handle(handle);
    if (session == nullptr || !session->renderable) return nullptr;

    const auto flags = static_cast<dmcresource::RenderFlags>(
        static_cast<std::uint32_t>(render_flags));

    dmcresource::ViewState view;
    view.yaw_radians = static_cast<float>(yaw);
    view.pitch_radians = std::clamp(static_cast<float>(pitch), -1.55f, 1.55f);
    view.zoom = std::clamp(static_cast<float>(zoom), 0.15f, 8.0f);
    view.wireframe = dmcresource::has_render_flag(
        flags, dmcresource::RenderFlag::Wireframe);
    view.uv_layout = dmcresource::has_render_flag(
        flags, dmcresource::RenderFlag::UvLayout);

    const int width = std::clamp(static_cast<int>(requested_width), 64, 1024);
    const int height = std::clamp(static_cast<int>(requested_height), 64, 1024);
    const auto* hierarchy =
        !view.uv_layout &&
        dmcresource::has_render_flag(flags, dmcresource::RenderFlag::Hierarchy) &&
        session->hierarchy_overlay.available()
            ? &session->hierarchy_overlay
            : nullptr;
    const auto* textures = session->attached_textures.empty()
        ? nullptr
        : &session->attached_textures;
    const auto image = dmcresource::render_view(session->render_mesh,
                                                width, height, view,
                                                hierarchy, textures);
    return image_to_argb(env, image);
}
