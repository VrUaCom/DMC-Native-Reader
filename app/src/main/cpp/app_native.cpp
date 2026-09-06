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

    // v2 reusable session state. These projections are produced by the native
    // module pipeline once and retained for inspector/render/JNI consumers.
    dmcresource::ResourceCapabilities capabilities{};
    dmcresource::InspectionDocument inspection;
    dmcresource::RenderScene scene;

    // Fallback geometry for promoted modules that have not yet migrated to
    // RenderScene. SCM/MOD rendering now consumes scene directly; HITS still
    // uses this compatibility path until its own IR migration.
    dmcresource::Mesh mesh;

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

jintArray image_to_argb(JNIEnv* env, const dmcresource::RgbaImage& image) {
    if (image.width <= 0 || image.height <= 0) return nullptr;
    const std::size_t count64 = static_cast<std::size_t>(image.width) *
                                static_cast<std::size_t>(image.height);
    if (count64 > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) return nullptr;
    if (image.pixels.size() != count64 * 4u) return nullptr;

    const jsize count = static_cast<jsize>(count64);
    jintArray result = env->NewIntArray(count);
    if (result == nullptr) return nullptr;

    std::vector<jint> argb(count64);
    for (std::size_t i = 0; i < count64; ++i) {
        const std::size_t o = i * 4u;
        const std::uint32_t r = image.pixels[o + 0];
        const std::uint32_t g = image.pixels[o + 1];
        const std::uint32_t b = image.pixels[o + 2];
        const std::uint32_t a = image.pixels[o + 3];
        argb[i] = static_cast<jint>((a << 24u) | (r << 16u) | (g << 8u) | b);
    }
    env->SetIntArrayRegion(result, 0, count, argb.data());
    return result;
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

    auto session = std::make_unique<Session>();
    session->probe = pipeline.probe;
    session->capabilities = pipeline.capabilities;
    session->inspection = std::move(pipeline.inspection);
    session->scene = std::move(pipeline.scene);
    session->mesh = std::move(pipeline.mesh);
    session->detail = std::move(pipeline.detail);
    session->trace = dmcresource::pipeline_trace(pipeline);
    session->renderable = pipeline.renderable;
    return to_handle(session.release());
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
        std::size_t vertices = session->mesh.vertices.size();
        std::size_t triangles = session->mesh.indices.size() / 3u;
        if (session->scene.has_geometry()) {
            vertices = 0U;
            triangles = 0U;
            for (const auto& primitive : session->scene.meshes) {
                vertices += primitive.mesh.vertices.size();
                triangles += primitive.mesh.indices.size() / 3U;
            }
        }
        out << " | vertices=" << vertices
            << " | triangles=" << triangles;
    } else {
        out << " | preview=inspection";
    }
    if (!session->detail.empty()) out << "\n" << session->detail;
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
        jboolean wireframe) {
    const Session* session = from_handle(handle);
    if (session == nullptr || !session->renderable) return nullptr;

    dmcresource::ViewState view;
    view.yaw_radians = static_cast<float>(yaw);
    view.pitch_radians = std::clamp(static_cast<float>(pitch), -1.55f, 1.55f);
    view.zoom = std::clamp(static_cast<float>(zoom), 0.15f, 8.0f);
    view.wireframe = wireframe == JNI_TRUE;

    const int width = std::clamp(static_cast<int>(requested_width), 64, 1024);
    const int height = std::clamp(static_cast<int>(requested_height), 64, 1024);
    const auto image = session->scene.has_geometry()
        ? dmcresource::render_view(session->scene, width, height, view)
        : dmcresource::render_view(session->mesh, width, height, view);
    return image_to_argb(env, image);
}
