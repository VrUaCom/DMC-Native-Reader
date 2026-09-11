#include "dmcresource/resource_limits.h"
#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/view_renderer.h"

namespace {

constexpr auto kMaxMappedBytes = dmcresource::resource_limits::kMaxResourceBytes;

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

using dmcresource::Session;

Session* from_handle(jlong handle) noexcept {
    return reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
}

jlong to_handle(Session* session) noexcept {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
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

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_open(
        JNIEnv* env, jclass, jint fd, jstring filename) {
    if (fd < 0) return 0;
    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) return 0;

    const auto name = to_utf8(env, filename);
    try {
        auto session = dmcresource::open_session(name, mapped.data(), mapped.size());
        return to_handle(session.release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_composeMods(
        JNIEnv* env, jclass, jlongArray handles, jobjectArray names) {
    if (handles == nullptr || names == nullptr) return 0;
    const jsize count = env->GetArrayLength(handles);
    if (count < 2 || env->GetArrayLength(names) != count) return 0;

    try {
        std::vector<jlong> raw_handles(static_cast<std::size_t>(count));
        env->GetLongArrayRegion(handles, 0, count, raw_handles.data());
        if (env->ExceptionCheck()) return 0;

        std::vector<const Session*> parts;
        std::vector<std::string> part_names;
        parts.reserve(static_cast<std::size_t>(count));
        part_names.reserve(static_cast<std::size_t>(count));
        for (jsize index = 0; index < count; ++index) {
            const Session* part = from_handle(raw_handles[static_cast<std::size_t>(index)]);
            if (part == nullptr) return 0;
            parts.push_back(part);

            auto* value = static_cast<jstring>(env->GetObjectArrayElement(names, index));
            if (env->ExceptionCheck()) return 0;
            part_names.push_back(to_utf8(env, value));
            if (value != nullptr) env->DeleteLocalRef(value);
        }

        auto composite = dmcresource::compose_mod_sessions(parts, part_names);
        return to_handle(composite.release());
    } catch (...) { return 0; }
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

    return env->NewStringUTF(dmcresource::describe_session(session).c_str());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_blackWidowState(
        JNIEnv*, jclass, jlong handle) {
    return static_cast<jlong>(black_widow_state(from_handle(handle)));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartCount(
        JNIEnv*, jclass, jlong handle) {
    const auto count = dmcresource::session_composite_part_count(from_handle(handle));
    if (count > static_cast<std::size_t>(std::numeric_limits<jint>::max())) return 0;
    return static_cast<jint>(count);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartName(
        JNIEnv* env, jclass, jlong handle, jint index) {
    return env->NewStringUTF(
        dmcresource::session_composite_part_name(from_handle(handle), index).c_str());
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

    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) {
        session->texture_attachment_detail =
            "PTX companion rejected: could not map selected file";
        return JNI_FALSE;
    }

    const auto name = to_utf8(env, filename);
    return dmcresource::attach_session_ptx(session, name, mapped.data(), mapped.size())
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachPtxToPart(
        JNIEnv* env, jclass, jlong handle, jint part_index,
        jint fd, jstring filename) {
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return JNI_FALSE;

    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) {
        session->texture_attachment_detail =
            "PTX companion rejected: could not map selected file";
        return JNI_FALSE;
    }

    const auto name = to_utf8(env, filename);
    return dmcresource::attach_session_part_ptx(
        session, part_index, name, mapped.data(), mapped.size())
        ? JNI_TRUE : JNI_FALSE;
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
        dmcresource::session_child_count(session) > static_cast<std::size_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(dmcresource::session_child_count(session));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceTitle(
        JNIEnv* env, jclass, jlong handle, jint index) {
    return env->NewStringUTF(dmcresource::session_child_title(from_handle(handle), index).c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewWidth(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto [w, h] = dmcresource::session_child_preview_size(from_handle(handle), index);
    return w <= static_cast<std::uint32_t>(std::numeric_limits<jint>::max())
        ? static_cast<jint>(w) : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewHeight(
        JNIEnv*, jclass, jlong handle, jint index) {
    const auto [w, h] = dmcresource::session_child_preview_size(from_handle(handle), index);
    return h <= static_cast<std::uint32_t>(std::numeric_limits<jint>::max())
        ? static_cast<jint>(h) : 0;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreview(
        JNIEnv* env, jclass, jlong handle, jint index) {
    try {
        dmcresource::ImagePreview scratch;
        const auto* image = dmcresource::session_child_preview(from_handle(handle), index, &scratch);
        return image ? preview_to_argb(env, *image) : nullptr;
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openChild(
        JNIEnv*, jclass, jlong handle, jint index) {
    try {
        return to_handle(dmcresource::open_session_child(from_handle(handle), index).release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openUvGallery(
        JNIEnv*, jclass, jlong handle) {
    try {
        return to_handle(dmcresource::open_uv_gallery(from_handle(handle)).release());
    } catch (...) { return 0; }
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
    if (session == nullptr) return nullptr;

    return image_to_argb(env, dmcresource::render_session(session,
        requested_width, requested_height, yaw, pitch, zoom,
        static_cast<std::uint32_t>(render_flags)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_inspectionTopic(
        JNIEnv* env, jclass, jlong handle, jint topic) {
    try {
        const auto document = dmcresource::inspect_session(from_handle(handle),
            static_cast<dmcresource::InspectionTopic>(topic));
        return env->NewStringUTF(dmcresource::format_inspection_tree(document).c_str());
    } catch (...) { return env->NewStringUTF("Information unavailable"); }
}
