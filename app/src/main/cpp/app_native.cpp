#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <mutex>
#include <unordered_set>
#include "dmcresource/resource_limits.h"

#include <android/bitmap.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/collision_debug.h"
#include "dmcresource/environment_collision.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/stage_room.h"
#include "dmcresource/stage_markers.h"
#include "dmcresource/camera_control.h"
#include "dmcresource/demo_session.h"
#include "dmcresource/spider/collision_report.h"
#include "dmcresource/spider/camera.h"
#include "dmcresource/spider/stage_actions.h"
#include "dmcresource/spider/app_info.h"
#include "dmcresource/spider/companion_menu.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/enemy_effects.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/model_placement_actions.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/view_gpu.h"
#include "dmcresource/view_benchmark.h"
#include "dmcresource/frame_profile.h"
#include "android/gles_view_backend.h"

#ifndef DMC_NATIVE_READER_BUILD_SHA
#define DMC_NATIVE_READER_BUILD_SHA "unknown"
#endif

namespace {

constexpr auto kMaxMappedBytes = dmcresource::resource_limits::kMaxResourceBytes;

std::string to_utf8(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* raw = env->GetStringUTFChars(value, nullptr);
    if (raw == nullptr) return {};
    try {
        std::string out(raw);
        env->ReleaseStringUTFChars(value, raw);
        return out;
    } catch (...) {
        env->ReleaseStringUTFChars(value, raw);
        throw;
    }
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

// Sessions are rendered on a worker thread while the UI thread opens,
// changes and closes them: every JNI entry that touches a session holds
// this lock, and a handle is only honoured while it is registered (a render
// queued for a session that has since been closed finds nothing).
std::recursive_mutex& session_mutex() noexcept {
    static std::recursive_mutex mutex;
    return mutex;
}

std::unordered_set<Session*>& live_sessions() noexcept {
    static std::unordered_set<Session*> live;
    return live;
}

using SessionLock = std::lock_guard<std::recursive_mutex>;

// Fly camera of the view (stages, collision views): set from the UI thread,
// read by every frame, pick and benchmark so they all see the same camera.
// The camera state (fly eye, orbit pivot) is the core's (camera_control.h).
dmcresource::ViewControls view_controls(float pan_x, float pan_y, float room_yaw, jboolean follow, float dolly) {
    dmcresource::ViewControls controls{pan_x, pan_y, room_yaw, follow == JNI_TRUE, dolly};
    const auto camera = dmcresource::camera::current();
    controls.fly = camera.fly;
    controls.eye = camera.eye;
    controls.has_pivot = camera.has_pivot;
    controls.pivot = camera.pivot;
    return controls;
}

Session* from_handle(jlong handle) noexcept {
    auto* session = reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
    if (session == nullptr) return nullptr;
    const SessionLock lock{session_mutex()};
    return live_sessions().count(session) != 0U ? session : nullptr;
}

jlong to_handle(Session* session) noexcept {
    if (session != nullptr) {
        const SessionLock lock{session_mutex()};
        try {
            live_sessions().insert(session);
        } catch (...) {
            delete session;
            return 0;
        }
    }
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
}

bool copy_rgba_to_bitmap(JNIEnv* env,
                         jobject bitmap,
                         std::size_t width,
                         std::size_t height,
                         const std::vector<std::uint8_t>& rgba) noexcept {
    if (env == nullptr || bitmap == nullptr || width == 0U || height == 0U) {
        return false;
    }
    if (width > std::numeric_limits<std::size_t>::max() / 4U) return false;
    const std::size_t row_bytes = width * 4U;
    if (height > std::numeric_limits<std::size_t>::max() / row_bytes) return false;
    if (rgba.size() != row_bytes * height) return false;
    if (width > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
        height > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return false;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 ||
        info.width != static_cast<std::uint32_t>(width) ||
        info.height != static_cast<std::uint32_t>(height) ||
        static_cast<std::size_t>(info.stride) < row_bytes) {
        return false;
    }

    void* raw_pixels = nullptr;
    const int lock_result = AndroidBitmap_lockPixels(env, bitmap, &raw_pixels);
    if (lock_result != ANDROID_BITMAP_RESULT_SUCCESS) {
        return false;
    }
    if (raw_pixels == nullptr) {
        (void)AndroidBitmap_unlockPixels(env, bitmap);
        return false;
    }

    auto* destination = static_cast<std::uint8_t*>(raw_pixels);
    for (std::size_t y = 0U; y < height; ++y) {
        std::memcpy(destination + y * static_cast<std::size_t>(info.stride),
                    rgba.data() + y * row_bytes,
                    row_bytes);
    }

    return AndroidBitmap_unlockPixels(env, bitmap) == ANDROID_BITMAP_RESULT_SUCCESS;
}

bool image_to_bitmap(JNIEnv* env,
                     jobject bitmap,
                     const dmcresource::RgbaImage& image) noexcept {
    if (image.width <= 0 || image.height <= 0) return false;
    return copy_rgba_to_bitmap(
        env, bitmap,
        static_cast<std::size_t>(image.width),
        static_cast<std::size_t>(image.height),
        image.pixels);
}

bool preview_to_bitmap(JNIEnv* env,
                       jobject bitmap,
                       const dmcresource::ImagePreview& image) noexcept {
    if (!image.available()) return false;
    return copy_rgba_to_bitmap(
        env, bitmap,
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

    try {
        const auto name = to_utf8(env, filename);
        auto session = dmcresource::open_session(name, mapped.data(), mapped.size());
        return to_handle(session.release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_composeMods(
        JNIEnv* env, jclass, jlongArray handles, jobjectArray names) {
    const SessionLock jni_lock{session_mutex()};
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

            jstring value = static_cast<jstring>(env->GetObjectArrayElement(names, index));
            if (env->ExceptionCheck()) return 0;
            part_names.push_back(to_utf8(env, value));
            if (value != nullptr) env->DeleteLocalRef(value);
        }

        auto composite = dmcresource::spider::actions::compose_mod_sessions(
            parts, part_names);
        return to_handle(composite.release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_close(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock lock{session_mutex()};
    auto* session = from_handle(handle);
    if (session == nullptr) return;
    live_sessions().erase(session);
    delete session;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_info(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("no session");
    try {
        const auto text = dmcresource::describe_session(session);
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF("Information unavailable"); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_buildIdentity(
        JNIEnv* env, jclass) {
    return env->NewStringUTF(DMC_NATIVE_READER_BUILD_SHA);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_diagnostics(
        JNIEnv* env, jclass, jlong handle) {
    try {
        const auto text =
            dmcresource::describe_session_diagnostics(from_handle(handle));
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_blackWidowState(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return static_cast<jlong>(black_widow_state(from_handle(handle)));
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartCount(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto count = dmcresource::session_composite_part_count(from_handle(handle));
        if (count > static_cast<std::size_t>(std::numeric_limits<jint>::max())) return 0;
        return static_cast<jint>(count);
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartName(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto name = dmcresource::session_composite_part_name(from_handle(handle), index);
        return env->NewStringUTF(name.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartState(
        JNIEnv* env, jclass, jlong handle, jint index) {
    try {
        const auto text =
            dmcresource::describe_composite_part_state(from_handle(handle), index);
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartNodeCount(
        JNIEnv*, jclass, jlong handle, jint part_index) {
    try {
        const auto count = dmcresource::session_composite_part_node_count(
            from_handle(handle), part_index);
        if (count > static_cast<std::size_t>(std::numeric_limits<jint>::max())) return 0;
        return static_cast<jint>(count);
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartNodeName(
        JNIEnv* env, jclass, jlong handle, jint part_index, jint node_index) {
    try {
        const auto name = dmcresource::session_composite_part_node_name(
            from_handle(handle), part_index, node_index);
        return env->NewStringUTF(name.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartDefaultAttachmentSelector(
        JNIEnv*, jclass, jlong handle, jint part_index) {
    try {
        const auto selector =
            dmcresource::session_composite_part_default_attachment_selector(
                from_handle(handle), part_index);
        if (!selector ||
            *selector > static_cast<std::uint32_t>(std::numeric_limits<jint>::max())) {
            return -1;
        }
        return static_cast<jint>(*selector);
    } catch (...) { return -1; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachModPartToHostJoint(
        JNIEnv* env, jclass, jlong handle, jint host_part_index,
        jint child_part_index, jint host_joint_index) {
    if (host_part_index < 0 || child_part_index < 0 || host_joint_index < 0) {
        return env->NewStringUTF("invalid-part-index");
    }
    const auto result = dmcresource::spider::actions::attach_mod_part_to_host_joint(
        from_handle(handle),
        static_cast<std::size_t>(host_part_index),
        static_cast<std::size_t>(child_part_index),
        static_cast<std::uint32_t>(host_joint_index));
    return env->NewStringUTF(dmcresource::composite_placement::to_string(result.status));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_resetModPartPlacement(
        JNIEnv* env, jclass, jlong handle, jint child_part_index) {
    if (child_part_index < 0) return env->NewStringUTF("invalid-part-index");
    const auto result = dmcresource::spider::actions::reset_mod_part_placement(
        from_handle(handle), static_cast<std::size_t>(child_part_index));
    return env->NewStringUTF(dmcresource::composite_placement::to_string(result.status));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreviewWidth(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    const auto* preview = dmcresource::session_active_preview(session);
    if (preview == nullptr || !preview->available() ||
        preview->width > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(preview->width);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreviewHeight(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    const auto* preview = dmcresource::session_active_preview(session);
    if (preview == nullptr || !preview->available() ||
        preview->height > static_cast<std::uint32_t>(
            std::numeric_limits<jint>::max())) {
        return 0;
    }
    return static_cast<jint>(preview->height);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreview(
        JNIEnv* env, jclass, jlong handle, jobject target) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    const auto* preview = dmcresource::session_active_preview(session);
    if (preview == nullptr) return JNI_FALSE;
    return preview_to_bitmap(env, target, *preview)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasDualPreview(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::session_has_dual_preview(from_handle(handle))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_infoPreviewActive(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::session_info_preview_active(from_handle(handle))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setInfoPreviewActive(
        JNIEnv*, jclass, jlong handle, jboolean active) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::session_set_info_preview(
        from_handle(handle), active == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachPtx(
        JNIEnv* env, jclass, jlong handle, jint fd, jstring filename) {
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return JNI_FALSE;

    try {
        ReadOnlyMap mapped(fd);
        if (!mapped.valid()) {
            session->texture_attachment_detail =
                "PTX companion rejected: could not map selected file";
            return JNI_FALSE;
        }

        const auto name = to_utf8(env, filename);
        return dmcresource::spider::actions::attach_ptx(
            session, name, mapped.data(), mapped.size())
            ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachPtxToPart(
        JNIEnv* env, jclass, jlong handle, jint part_index,
        jint fd, jstring filename) {
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return JNI_FALSE;

    try {
        ReadOnlyMap mapped(fd);
        if (!mapped.valid()) {
            session->texture_attachment_detail =
                "PTX companion rejected: could not map selected file";
            return JNI_FALSE;
        }

        const auto name = to_utf8(env, filename);
        return dmcresource::spider::actions::attach_ptx_to_part(
            session, part_index, name, mapped.data(), mapped.size())
            ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_textureAttachmentInfo(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    return env->NewStringUTF(session->texture_attachment_detail.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceCount(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const Session* session = from_handle(handle);
        if (session == nullptr ||
            dmcresource::session_child_count(session) > static_cast<std::size_t>(
                std::numeric_limits<jint>::max())) {
            return 0;
        }
        return static_cast<jint>(dmcresource::session_child_count(session));
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceTitle(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto title = dmcresource::session_child_title(from_handle(handle), index);
        return env->NewStringUTF(title.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewWidth(
        JNIEnv*, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto [w, h] = dmcresource::session_child_preview_size(from_handle(handle), index);
        (void)h;
        return w <= static_cast<std::uint32_t>(std::numeric_limits<jint>::max())
            ? static_cast<jint>(w) : 0;
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewHeight(
        JNIEnv*, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto [w, h] = dmcresource::session_child_preview_size(from_handle(handle), index);
        (void)w;
        return h <= static_cast<std::uint32_t>(std::numeric_limits<jint>::max())
            ? static_cast<jint>(h) : 0;
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreview(
        JNIEnv* env, jclass, jlong handle, jint index, jobject target) {
    const SessionLock jni_lock{session_mutex()};
    try {
        dmcresource::ImagePreview scratch;
        const auto* image = dmcresource::session_child_preview(
            from_handle(handle), index, &scratch);
        return image != nullptr && preview_to_bitmap(env, target, *image)
            ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openChild(
        JNIEnv*, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return to_handle(dmcresource::open_session_child(from_handle(handle), index).release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openUvGallery(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return to_handle(dmcresource::open_uv_gallery(from_handle(handle)).release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_inspection(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    try {
        const auto text = dmcresource::format_inspection_tree(session->inspection);
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF("Information unavailable"); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_render(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jobject target) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;

    try {
        const auto image = dmcresource::render_session(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags));
        return image_to_bitmap(env, target, image) ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

// Render with the gesture controls (pan, room twist, camera follow).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_renderEx(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw,
        jboolean follow, jfloat dolly, jobject target) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    try {
        const auto controls = view_controls(pan_x, pan_y, room_yaw, follow, dolly);
        const auto image = dmcresource::render_session(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags), controls);
        return image_to_bitmap(env, target, image) ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

// Worker-thread frame: optionally pose the bound MOT at `motion_frame`
// (NaN: leave the pose), render with the gesture controls and write RGBA8
// rows into a direct ByteBuffer of width * height * 4 bytes. Returns 0 on
// failure, 1 on success, 2 when the motion could not be posed.
extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_renderToBuffer(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw,
        jboolean follow, jfloat dolly, jfloat motion_frame, jobject buffer) {
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || buffer == nullptr) return 0;
    try {
        namespace profile = dmcresource::frame_profile;
        jint status = 1;
        {
            const profile::Scope timed{profile::Stage::Pose};
            if (std::isfinite(motion_frame) && !dmcresource::motion::apply_motion_frame(session, motion_frame)) {
                status = 2;
            }
        }
        auto controls = view_controls(pan_x, pan_y, room_yaw, follow, dolly);
        controls.present = true;  // the view's own frame: it may go straight to the window
        // The viewfinder, when it is on, shows the lens and the look.
        const auto image = dmcresource::spider::camera::render_view_frame(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags), controls);
        if (image.width != requested_width || image.height != requested_height) return 0;
        // Shown on the window already (status bit 4): nothing to copy.
        if (image.pixels.empty()) {
            profile::frame_done();
            return status | 4;
        }
        auto* out = static_cast<std::uint8_t*>(env->GetDirectBufferAddress(buffer));
        const auto capacity = env->GetDirectBufferCapacity(buffer);
        if (out == nullptr || capacity < 0 ||
            static_cast<std::size_t>(capacity) < image.pixels.size()) {
            return 0;
        }
        {
            const profile::Scope timed{profile::Stage::CopyOut};
            std::memcpy(out, image.pixels.data(), image.pixels.size());
        }
        profile::frame_done();
        return status;
    } catch (...) { return 0; }
}

// The photo and video camera (core spider/camera.h). Settings: one row per
// setting, "id\tsection\tlabel\thint\tstart\toption\toption...".
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraSettings(JNIEnv* env, jclass) {
    try {
        const auto& list = dmcresource::spider::camera::settings();
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(list.size()), env->FindClass("java/lang/String"), nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& s = list[i];
            std::string row = s.id + "\t" + s.section + "\t" + s.label + "\t" + s.hint + "\t" + std::to_string(s.start);
            for (const auto& option : s.options) row += "\t" + option;
            jstring text = env->NewStringUTF(row.c_str());
            env->SetObjectArrayElement(out, static_cast<jsize>(i), text);
            env->DeleteLocalRef(text);
        }
        return out;
    } catch (...) { return nullptr; }
}

// The chosen option of every setting, in cameraSettings() order.
extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setCameraChoices(JNIEnv* env, jclass, jintArray choices) {
    try {
        std::vector<int> values;
        if (choices != nullptr) {
            const jsize n = env->GetArrayLength(choices);
            values.resize(static_cast<std::size_t>(n));
            env->GetIntArrayRegion(choices, 0, n, reinterpret_cast<jint*>(values.data()));
        }
        dmcresource::spider::camera::set_choices(values);
    } catch (...) {}
}

// A picture (kind 0) or a video (1) of a view of that size: {width, height,
// format, quality / bits per second, fps, key frame ms, limit s, timer s,
// burst, subtitles}.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraPlan(JNIEnv* env, jclass, jint kind, jint view_width,
                                                         jint view_height) {
    namespace cam = dmcresource::spider::camera;
    const auto p = cam::plan(kind == 1 ? cam::Kind::Video : cam::Kind::Photo, view_width, view_height);
    const jint values[10] = {p.width, p.height, p.format, p.quality, p.fps, p.keyframe_ms,
                             p.limit_seconds, p.timer_seconds, p.burst, p.subtitles ? 1 : 0};
    jintArray out = env->NewIntArray(10);
    if (out != nullptr) env->SetIntArrayRegion(out, 0, 10, values);
    return out;
}

// A picture into a bitmap of cameraPlan's size: the view's camera framed for
// the picture's shape, developed with the settings.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_renderCapture(
        JNIEnv* env, jclass, jlong handle, jint kind, jint view_width, jint view_height, jfloat yaw,
        jfloat pitch, jfloat zoom, jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw,
        jboolean follow, jfloat dolly, jfloat motion_frame, jfloat motion_fps, jobject target) {
    namespace cam = dmcresource::spider::camera;
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    try {
        // The scene moving while the shutter is open: the playing motion.
        const cam::SceneMotion motion{motion_frame, motion_fps};
        const auto image = cam::render_capture(session, kind == 1 ? cam::Kind::Video : cam::Kind::Photo,
                                               view_width, view_height, yaw, pitch, zoom,
                                               static_cast<std::uint32_t>(render_flags),
                                               view_controls(pan_x, pan_y, room_yaw, follow, dolly), motion);
        return image_to_bitmap(env, target, image) ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

// A video frame on the render thread, like renderToBuffer at cameraPlan(1)'s
// size; the buffer holds width * height * 4 bytes.
extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_renderVideoFrame(
        JNIEnv* env, jclass, jlong handle, jint view_width, jint view_height, jfloat yaw, jfloat pitch,
        jfloat zoom, jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw, jboolean follow,
        jfloat dolly, jfloat motion_frame, jobject buffer) {
    namespace cam = dmcresource::spider::camera;
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || buffer == nullptr) return 0;
    try {
        jint status = 1;
        if (std::isfinite(motion_frame) && !dmcresource::motion::apply_motion_frame(session, motion_frame)) {
            status = 2;
        }
        const auto p = cam::plan(cam::Kind::Video, view_width, view_height);
        const auto image = cam::render_capture(session, cam::Kind::Video, view_width, view_height, yaw, pitch,
                                               zoom, static_cast<std::uint32_t>(render_flags),
                                               view_controls(pan_x, pan_y, room_yaw, follow, dolly));
        if (image.width != p.width || image.height != p.height) return 0;
        auto* out = static_cast<std::uint8_t*>(env->GetDirectBufferAddress(buffer));
        const auto capacity = env->GetDirectBufferCapacity(buffer);
        if (out == nullptr || capacity < 0 || static_cast<std::size_t>(capacity) < image.pixels.size()) return 0;
        std::memcpy(out, image.pixels.data(), image.pixels.size());
        return status;
    } catch (...) { return 0; }
}

// A RAW picture (DNG) into the open file descriptor fd; `developed`, when not
// null, gets the developed picture (RAW + JPEG; cameraPlan(0)'s size).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_writeCaptureDng(
        JNIEnv* env, jclass, jlong handle, jint view_width, jint view_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw, jboolean follow, jfloat dolly,
        jfloat motion_frame, jfloat motion_fps, jint fd, jobject developed) {
    namespace cam = dmcresource::spider::camera;
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return JNI_FALSE;
    try {
        dmcresource::RgbaImage image;
        const bool ok = cam::capture_raw(session, view_width, view_height, yaw, pitch, zoom,
                                         static_cast<std::uint32_t>(render_flags),
                                         view_controls(pan_x, pan_y, room_yaw, follow, dolly), fd,
                                         developed != nullptr ? &image : nullptr,
                                         cam::SceneMotion{motion_frame, motion_fps});
        if (!ok) return JNI_FALSE;
        if (developed != nullptr && !image_to_bitmap(env, developed, image)) return JNI_FALSE;
        return JNI_TRUE;
    } catch (...) { return JNI_FALSE; }
}

// Camera mode (the core's setting): the shell swaps its bottom bars for the
// camera's while it is on.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraMode(JNIEnv*, jclass) {
    return dmcresource::spider::camera::finder_on() ? JNI_TRUE : JNI_FALSE;
}

// What else turning setting `index` to `option` changes: {message,
// "index\toption", ...} (empty: nothing).
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraFollowUps(JNIEnv* env, jclass, jint index, jint option) {
    try {
        namespace cam = dmcresource::spider::camera;
        if (index < 0 || index >= static_cast<jint>(cam::Key::Count)) return nullptr;
        const auto follow = cam::follow_up(static_cast<cam::Key>(index), option);
        if (follow.changes.empty()) return nullptr;
        std::vector<std::string> rows{follow.message};
        for (const auto& change : follow.changes) {
            rows.push_back(std::to_string(static_cast<int>(change.key)) + "\t" + std::to_string(change.option));
        }
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(rows.size()), env->FindClass("java/lang/String"), nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            jstring text = env->NewStringUTF(rows[i].c_str());
            env->SetObjectArrayElement(out, static_cast<jsize>(i), text);
            env->DeleteLocalRef(text);
        }
        return out;
    } catch (...) { return nullptr; }
}

// The screen keeps its orientation (the phone aims the camera).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraHoldsOrientation(JNIEnv*, jclass) {
    return dmcresource::spider::camera::holds_screen_orientation() ? JNI_TRUE : JNI_FALSE;
}

// Whether a cutscene's camera drives the view: camera mode films through it
// (no free camera, the gyroscope waits) until a finger takes it.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraFollowsCuts(JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::spider::camera::follows_cuts(from_handle(handle)) ? JNI_TRUE : JNI_FALSE;
}

// The camera bar's dials: "setting index[,index ...]\ttitle\tvalue" per dial
// (the first index is the dial's setting, the rest go with it).
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraDials(JNIEnv* env, jclass) {
    try {
        const auto dials = dmcresource::spider::camera::dials();
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(dials.size()), env->FindClass("java/lang/String"), nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t i = 0; i < dials.size(); ++i) {
            std::string keys = std::to_string(static_cast<int>(dials[i].key));
            for (const auto key : dials[i].with) keys += "," + std::to_string(static_cast<int>(key));
            const std::string row = keys + "\t" + dials[i].title + "\t" + dials[i].value;
            jstring text = env->NewStringUTF(row.c_str());
            env->SetObjectArrayElement(out, static_cast<jsize>(i), text);
            env->DeleteLocalRef(text);
        }
        return out;
    } catch (...) { return nullptr; }
}

// Walking (core camera, ARCore in the shell): the phone's steps move the
// drone's eye; false when it does not walk.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_walkActive(JNIEnv*, jclass) {
    return dmcresource::spider::camera::walk_active() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_walk(JNIEnv*, jclass, jfloat yaw, jfloat forward, jfloat right,
                                                   jfloat up) {
    return dmcresource::spider::camera::walk(yaw, forward, right, up) ? JNI_TRUE : JNI_FALSE;
}

// Gyroscope (core camera): active in camera mode with it on; a reading of
// the phone (heading, elevation, roll) gives the camera's turn {yaw, pitch}.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_gyroActive(JNIEnv*, jclass) {
    return dmcresource::spider::camera::gyro_active() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_gyroTurn(JNIEnv* env, jclass, jfloat heading, jfloat elevation,
                                                       jfloat roll) {
    const auto turn = dmcresource::spider::camera::gyro_turn(heading, elevation, roll);
    jfloatArray out = env->NewFloatArray(2);
    if (out != nullptr) env->SetFloatArrayRegion(out, 0, 2, turn.data());
    return out;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_gyroReset(JNIEnv*, jclass) {
    dmcresource::spider::camera::gyro_reset();
}

// The camera's roll: turned by radians (0 reads it).
extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraRoll(JNIEnv*, jclass, jfloat radians) {
    return dmcresource::spider::camera::turn_roll(radians);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraLevel(JNIEnv*, jclass) {
    dmcresource::spider::camera::level_camera();
}

// Focus where a finger tapped (view render pixels); the line to show.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraFocusAt(
        JNIEnv* env, jclass, jlong handle, jint width, jint height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw, jboolean follow, jfloat dolly, jfloat x,
        jfloat y) {
    namespace cam = dmcresource::spider::camera;
    const SessionLock jni_lock{session_mutex()};
    try {
        const float units = cam::focus_at(from_handle(handle), width, height, yaw, pitch, zoom,
                                          static_cast<std::uint32_t>(render_flags),
                                          view_controls(pan_x, pan_y, room_yaw, follow, dolly), x, y);
        return env->NewStringUTF(cam::focus_message(units).c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraFocusLock(JNIEnv* env, jclass) {
    namespace cam = dmcresource::spider::camera;
    const bool locked = cam::toggle_focus_lock();
    const std::string text = locked ? "AF lock: " + cam::focus_message(cam::focus_point()) : "Auto focus";
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraFocusRing(JNIEnv* env, jclass, jfloat steps) {
    namespace cam = dmcresource::spider::camera;
    return env->NewStringUTF(cam::focus_message(cam::turn_focus_ring(steps)).c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraPolarizerTurnable(JNIEnv*, jclass) {
    return dmcresource::spider::camera::polarizer_turnable() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraPolarizer(JNIEnv*, jclass, jfloat turn) {
    return dmcresource::spider::camera::turn_polarizer(turn);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setCameraPolarizer(JNIEnv*, jclass, jfloat radians) {
    dmcresource::spider::camera::set_polarizer_angle(radians);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_beginVideo(JNIEnv*, jclass) {
    dmcresource::spider::camera::begin_video();
}

// What the viewfinder draws over the view: {float[] {shown, x, y, w, h,
// then x0, y0, x1, y1 per grid line}, the settings line}.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_viewfinder(JNIEnv* env, jclass, jlong handle, jint width,
                                                         jint height, jfloat zoom, jboolean recording,
                                                         jint seconds) {
    try {
        dmcresource::spider::camera::Finder finder;
        {
            const SessionLock jni_lock{session_mutex()};
            finder = dmcresource::spider::camera::viewfinder(from_handle(handle), width, height, zoom,
                                                              recording == JNI_TRUE, seconds);
        }
        std::vector<jfloat> numbers{finder.shown ? 1.0F : 0.0F, finder.frame.x, finder.frame.y,
                                    finder.frame.width, finder.frame.height};
        for (const auto& line : finder.lines) numbers.insert(numbers.end(), line.begin(), line.end());
        jobjectArray out = env->NewObjectArray(2, env->FindClass("java/lang/Object"), nullptr);
        if (out == nullptr) return nullptr;
        jfloatArray floats = env->NewFloatArray(static_cast<jsize>(numbers.size()));
        if (floats == nullptr) return nullptr;
        env->SetFloatArrayRegion(floats, 0, static_cast<jsize>(numbers.size()), numbers.data());
        env->SetObjectArrayElement(out, 0, floats);
        jstring status = env->NewStringUTF(finder.status.c_str());
        env->SetObjectArrayElement(out, 1, status);
        return out;
    } catch (...) { return nullptr; }
}

// Camera distance (model units) at dolly 0, for the gesture readout; 0 when
// nothing is framed. The limit is the session's largest dolly.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_cameraMetrics(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    const jfloat values[2] = {dmcresource::session_camera_distance(session),
                              dmcresource::session_dolly_limit(session)};
    jfloatArray out = env->NewFloatArray(2);
    if (out != nullptr) env->SetFloatArrayRegion(out, 0, 2, values);
    return out;
}

// What is under image pixel (x, y): "model|<joint>", "room|<joint>",
// "placed|<joint>" (place = true and an upward room surface was hit: the model now
// stands there) or "none|<joint>"; <joint> is empty when no joint is near.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_pickView(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw,
        jboolean follow, jfloat dolly, jfloat x, jfloat y, jboolean place) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return nullptr;
    try {
        const auto controls = view_controls(pan_x, pan_y, room_yaw, follow, dolly);
        const auto pick = dmcresource::pick_session(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags), controls, x, y);
        std::string kind = pick.model ? "model" : pick.room ? "room" : "none";
        if (pick.room && pick.room_floor && !pick.model && place == JNI_TRUE) {
            dmcresource::stage_room::place_at(pick.room_point);
            kind = "placed";
        }
        const auto text = kind + "|" + pick.joint_name;
        return env->NewStringUTF(text.c_str());
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_inspectionTopic(
        JNIEnv* env, jclass, jlong handle, jint topic) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto document = dmcresource::inspect_session(from_handle(handle),
            static_cast<dmcresource::InspectionTopic>(topic));
        return env->NewStringUTF(dmcresource::format_inspection_tree(document).c_str());
    } catch (...) { return env->NewStringUTF("Information unavailable"); }
}

// --- Read-only PAC assembly and MOT playback (v34) -------------------------
// Java supplies handles/file descriptors only; binding, evaluation, skinning
// and archive classification live in DMCNativeReader::Core. All calls happen
// on the UI thread that also renders, so a Session is never posed and drawn
// concurrently.

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_assemblePac(
        JNIEnv* env, jclass, jlong handle, jstring archive_name) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return 0;
    try {
        const auto name = to_utf8(env, archive_name);
        return to_handle(dmcresource::pac_assembly::assemble_pac(
            *session, nullptr, name).release());
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLibraryCount(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return 0;
    const auto count = session->motion_library.size();
    return count > static_cast<std::size_t>(std::numeric_limits<jint>::max())
        ? std::numeric_limits<jint>::max() : static_cast<jint>(count);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLibraryName(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= session->motion_library.size()) {
        return env->NewStringUTF("");
    }
    try {
        return env->NewStringUTF(
            session->motion_library[static_cast<std::size_t>(index)].name.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLibraryPackSlot(
        JNIEnv*, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= session->motion_library.size()) {
        return -1;
    }
    return static_cast<jint>(
        session->motion_library[static_cast<std::size_t>(index)].pack_slot);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionScriptCount(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto count = dmcresource::motion::motion_script_count(from_handle(handle));
    return count > static_cast<std::size_t>(std::numeric_limits<jint>::max())
        ? std::numeric_limits<jint>::max()
        : static_cast<jint>(count);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionScriptSlot(
        JNIEnv*, jclass, jlong handle, jint script_index) {
    const SessionLock jni_lock{session_mutex()};
    if (script_index < 0) return -1;
    const auto slot = dmcresource::motion::motion_script_slot(
        from_handle(handle), static_cast<std::size_t>(script_index));
    return slot == std::numeric_limits<std::uint32_t>::max() ||
                   slot > static_cast<std::uint32_t>(
                              std::numeric_limits<jint>::max())
        ? -1
        : static_cast<jint>(slot);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionScriptCanPlayMotion(
        JNIEnv*, jclass, jlong handle, jint script_index, jint motion_index) {
    const SessionLock jni_lock{session_mutex()};
    if (script_index < 0 || motion_index < 0) return JNI_FALSE;
    return dmcresource::motion::motion_script_can_play_motion(
               from_handle(handle),
               static_cast<std::size_t>(script_index),
               static_cast<std::size_t>(motion_index))
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadLibraryMotionScript(
        JNIEnv* env, jclass, jlong handle, jint script_index, jint motion_index) {
    const SessionLock jni_lock{session_mutex()};
    if (script_index < 0 || motion_index < 0) {
        return env->NewStringUTF("MotionScript: invalid index");
    }
    try {
        auto report = dmcresource::motion::load_scripted_motion(
            from_handle(handle),
            static_cast<std::size_t>(script_index),
            static_cast<std::size_t>(motion_index));
        return env->NewStringUTF(report.detail.c_str());
    } catch (...) {
        return env->NewStringUTF("MotionScript: load failed");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadLibraryMotion(
        JNIEnv* env, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= session->motion_library.size()) {
        return env->NewStringUTF("Motion: invalid library index");
    }
    try {
        const auto report = dmcresource::motion::load_library_motion(
            session, static_cast<std::size_t>(index));
        return env->NewStringUTF(report.detail.c_str());
    } catch (...) { return env->NewStringUTF("Motion: load failed"); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadMotion(
        JNIEnv* env, jclass, jlong handle, jint fd, jstring filename) {
    const SessionLock jni_lock{session_mutex()};
    Session* session = from_handle(handle);
    if (session == nullptr || fd < 0) return env->NewStringUTF("Motion: no model session");
    try {
        ReadOnlyMap mapped(fd);
        if (!mapped.valid()) return env->NewStringUTF("Motion: could not map selected file");
        const auto name = to_utf8(env, filename);
        const auto report = dmcresource::motion::load_motion(
            session, name, mapped.data(), mapped.size());
        return env->NewStringUTF(report.detail.c_str());
    } catch (...) { return env->NewStringUTF("Motion: load failed"); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasMotion(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::motion::has_motion(from_handle(handle)) ? JNI_TRUE : JNI_FALSE;
}

// Viewer room (stage_room.h): built from a file descriptor, kept natively and
// drawn around every non-stage model while RenderFlag::Room is set.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadRoom(
        JNIEnv* env, jclass, jint fd, jstring filename) {
    if (fd < 0) return nullptr;
    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) return nullptr;
    try {
        const auto name = to_utf8(env, filename);
        auto room = dmcresource::stage_room::build_room(name, mapped.data(), mapped.size());
        if (!room) return nullptr;
        const auto detail = room->detail;
        dmcresource::stage_room::set_current(std::move(room));
        return env->NewStringUTF(detail.c_str());
    } catch (...) { return nullptr; }
}

// The stage's effect bank (st*_effect.pac): the layout's effects play from it.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadRoomEffects(
        JNIEnv* env, jclass, jint fd, jstring filename) {
    if (fd < 0) return nullptr;
    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) return nullptr;
    try {
        const auto name = to_utf8(env, filename);
        auto host = dmcresource::stage_room::make_effect_host(name, mapped.data(), mapped.size());
        if (!host) return nullptr;
        std::string detail = std::to_string(host->effect_banks.front().bank.records.size()) + " effect records, " +
                             std::to_string(host->effect_banks.front().textures.size()) + " textures";
        dmcresource::stage_room::set_effect_host(std::move(host));
        return env->NewStringUTF(detail.c_str());
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_clearRoomEffects(JNIEnv*, jclass) {
    dmcresource::stage_room::set_effect_host(nullptr);
}

// True when the room drawn with the session moves on its own (core
// room_animated: texture scrolls, effects): the viewer then redraws on a timer.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomAnimated(JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::room_animated(from_handle(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setRoomBroken(JNIEnv*, jclass, jboolean broken) {
    dmcresource::stage_room::set_broken(broken == JNI_TRUE);
}

// Kinds (distinct flag values) of the HITS shown with the session: its own
// file, its stage scene, or the room around it. One string per kind:
// "flags|records|floors|walls|ceilings|rgb".
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_collisionKinds(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    try {
        const auto kinds = dmcresource::spider::actions::shown_hits_kinds(session);
        jclass string_class = env->FindClass("java/lang/String");
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(kinds.size()), string_class, nullptr);
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const auto color = dmcresource::collision_class_color(dmcresource::collision_color_class(kinds[i].flags));
            char text[128];
            std::snprintf(text, sizeof text, "%u|%zu|%zu|%zu|%zu|%u", kinds[i].flags, kinds[i].count,
                          kinds[i].floors, kinds[i].walls, kinds[i].ceilings,
                          (static_cast<unsigned>(color[0]) << 16U) | (static_cast<unsigned>(color[1]) << 8U) |
                              static_cast<unsigned>(color[2]));
            env->SetObjectArrayElement(out, static_cast<jsize>(i), env->NewStringUTF(text));
        }
        return out;
    } catch (...) { return nullptr; }
}

// The core's account of the session's collision (spider collision_report.h):
// {title, summary, then one entry a group: "rgb|heading|line\nline..."},
// rgb -1 for a group without a colour. `attack`: the hitboxes, else HITS.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_collisionReport(JNIEnv* env, jclass, jlong handle, jboolean attack) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto* session = from_handle(handle);
        const auto report = attack == JNI_TRUE ? dmcresource::spider::actions::attack_collision_report(session)
                                               : dmcresource::spider::actions::stage_collision_report(session);
        std::vector<std::string> entries{report.title, report.summary};
        for (const auto& group : report.groups) {
            const long rgb = group.coloured ? (static_cast<long>(group.colour[0]) << 16) |
                                                  (static_cast<long>(group.colour[1]) << 8) | group.colour[2]
                                            : -1L;
            std::string entry = std::to_string(rgb) + "|" + group.heading + "|";
            for (std::size_t i = 0; i < group.lines.size(); ++i) {
                if (i > 0U) entry.push_back('\n');
                entry += group.lines[i];
            }
            entries.push_back(std::move(entry));
        }
        jclass string_class = env->FindClass("java/lang/String");
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(entries.size()), string_class, nullptr);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            env->SetObjectArrayElement(out, static_cast<jsize>(i), env->NewStringUTF(entries[i].c_str()));
        }
        return out;
    } catch (...) { return nullptr; }
}

// A stage archive opened as its assembled scene (stage_room::open_stage);
// 0 when it is not a stage.
extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_openStage(
        JNIEnv* env, jclass, jint fd, jstring filename) {
    if (fd < 0) return 0;
    ReadOnlyMap mapped(fd);
    if (!mapped.valid()) return 0;
    try {
        const auto name = to_utf8(env, filename);
        auto stage = dmcresource::stage_room::open_stage(name, mapped.data(), mapped.size());
        if (!stage) return 0;
        const SessionLock jni_lock{session_mutex()};
        return to_handle(stage.release());
    } catch (...) { return 0; }
}

// The start screen's Info, written by the core from the module registry.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_welcomeText(JNIEnv* env, jclass, jstring app_version) {
    try {
        return env->NewStringUTF(
            dmcresource::spider::actions::welcome_text(to_utf8(env, app_version), DMC_NATIVE_READER_BUILD_SHA)
                .c_str());
    } catch (...) { return nullptr; }
}

// ---- Spider stage / sound / export actions (spider/stage_actions.h) ----
//
// JNI only maps handles, file descriptors and bitmaps and hands the typed
// results to passive Java records; every decision and every line of text
// comes from the C++ action.

namespace {

namespace stage_actions = dmcresource::spider::actions;

jstring utf(JNIEnv* env, const std::string& text) { return env->NewStringUTF(text.c_str()); }

jobjectArray utf_array(JNIEnv* env, const std::vector<std::string>& values) {
    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(values.size()), string_class, nullptr);
    for (std::size_t i = 0; out != nullptr && i < values.size(); ++i) {
        jstring value = utf(env, values[i]);
        env->SetObjectArrayElement(out, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return out;
}

jintArray int_array(JNIEnv* env, const std::vector<int>& values) {
    jintArray out = env->NewIntArray(static_cast<jsize>(values.size()));
    if (out != nullptr && !values.empty()) {
        env->SetIntArrayRegion(out, 0, static_cast<jsize>(values.size()), reinterpret_cast<const jint*>(values.data()));
    }
    return out;
}

// ActionOutcome(boolean ok, int step, String message, int marker, String title, long session)
jobject outcome_object(JNIEnv* env, const stage_actions::ActionOutcome& o, jlong opened = 0) {
    jclass type = env->FindClass("com/dmcrengine/nativeviewer/ActionOutcome");
    if (type == nullptr) return nullptr;
    jmethodID make = env->GetMethodID(type, "<init>", "(ZILjava/lang/String;ILjava/lang/String;J)V");
    if (make == nullptr) return nullptr;
    return env->NewObject(type, make, o.ok ? JNI_TRUE : JNI_FALSE, static_cast<jint>(o.step), utf(env, o.message),
                          static_cast<jint>(o.marker), utf(env, o.title), opened);
}

// NativeMenu(String title, String message, int[] ids, String[] labels, String[] icons)
jobject menu_object(JNIEnv* env, const stage_actions::Menu& menu) {
    jclass type = env->FindClass("com/dmcrengine/nativeviewer/NativeMenu");
    if (type == nullptr) return nullptr;
    jmethodID make = env->GetMethodID(
        type, "<init>", "(Ljava/lang/String;Ljava/lang/String;[I[Ljava/lang/String;[Ljava/lang/String;)V");
    if (make == nullptr) return nullptr;
    std::vector<int> ids;
    std::vector<std::string> labels, icons;
    for (const auto& item : menu.items) {
        ids.push_back(item.id);
        labels.push_back(item.label);
        icons.push_back(item.icon);
    }
    return env->NewObject(type, make, utf(env, menu.title), utf(env, menu.message), int_array(env, ids),
                          utf_array(env, labels), utf_array(env, icons));
}

}  // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadStageConfig(JNIEnv* env, jclass, jint fd, jstring filename) {
    stage_actions::ActionOutcome outcome;
    try {
        const auto name = to_utf8(env, filename);
        if (fd >= 0) {
            ReadOnlyMap mapped(fd);
            const SessionLock jni_lock{session_mutex()};
            outcome = stage_actions::load_stage_config(name, mapped.valid() ? mapped.data() : nullptr,
                                                       mapped.valid() ? mapped.size() : 0U);
        } else {
            outcome = stage_actions::load_stage_config(name, nullptr, 0U);
        }
    } catch (...) {
    }
    return outcome_object(env, outcome);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_holdView(
        JNIEnv* env, jclass, jlong handle, jint requested_width,
        jint requested_height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw,
        jboolean follow, jfloat dolly, jfloat x, jfloat y, jboolean joints, jboolean scrub) {
    const SessionLock jni_lock{session_mutex()};
    const auto controls = view_controls(pan_x, pan_y, room_yaw, follow, dolly);
    return outcome_object(env, stage_actions::hold_view(from_handle(handle), requested_width, requested_height, yaw,
                                                        pitch, zoom, static_cast<std::uint32_t>(render_flags),
                                                        controls, x, y, joints == JNI_TRUE, scrub == JNI_TRUE));
}

// MarkerCard(String title, String[] names, String[] values, NativeMenu actions)
extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_markerCard(JNIEnv* env, jclass, jint marker) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto card = stage_actions::marker_card(marker);
        if (card.title.empty()) return nullptr;
        std::vector<std::string> names, values;
        for (const auto& [name, value] : card.rows) {
            names.push_back(name);
            values.push_back(value);
        }
        stage_actions::Menu buttons{.title = card.title, .message = {}, .items = card.actions};
        jclass type = env->FindClass("com/dmcrengine/nativeviewer/MarkerCard");
        if (type == nullptr) return nullptr;
        jmethodID make = env->GetMethodID(
            type, "<init>",
            "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;Lcom/dmcrengine/nativeviewer/NativeMenu;)V");
        if (make == nullptr) return nullptr;
        return env->NewObject(type, make, utf(env, card.title), utf_array(env, names), utf_array(env, values),
                              menu_object(env, buttons));
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_runMarkerCardAction(JNIEnv* env, jclass, jint marker, jint id) {
    const SessionLock jni_lock{session_mutex()};
    std::unique_ptr<Session> opened;
    const auto outcome = stage_actions::run_marker_card_action(marker, id, &opened);
    return outcome_object(env, outcome, opened ? to_handle(opened.release()) : 0);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_soundMenu(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return menu_object(env, stage_actions::sound_menu(from_handle(handle)));
    } catch (...) { return nullptr; }
}

// SoundPcm(int sampleRate, short[] samples, String message)
extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_soundPcm(JNIEnv* env, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto pcm = stage_actions::sound_pcm(from_handle(handle), index);
        jshortArray samples = env->NewShortArray(static_cast<jsize>(pcm.samples.size()));
        if (samples == nullptr) return nullptr;
        if (!pcm.samples.empty()) {
            env->SetShortArrayRegion(samples, 0, static_cast<jsize>(pcm.samples.size()),
                                     reinterpret_cast<const jshort*>(pcm.samples.data()));
        }
        jclass type = env->FindClass("com/dmcrengine/nativeviewer/SoundPcm");
        if (type == nullptr) return nullptr;
        jmethodID make = env->GetMethodID(type, "<init>", "(I[SLjava/lang/String;)V");
        if (make == nullptr) return nullptr;
        return env->NewObject(type, make, static_cast<jint>(pcm.sample_rate), samples, utf(env, pcm.message));
    } catch (...) { return nullptr; }
}

// The ⋮ menu (spider/companion_menu.h). CompanionMenu(NativeMenu shortcuts,
// NativeMenu rows, int[] dimmed).
extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_companionMenu(
        JNIEnv* env, jclass, jlong handle, jboolean root_scene, jboolean assembled_pac_source,
        jboolean stage_source, jboolean room_loaded, jboolean room_shown, jboolean recording) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto menu = dmcresource::spider::actions::companion_menu(
            from_handle(handle), {.root_scene = root_scene == JNI_TRUE,
                                  .assembled_pac_source = assembled_pac_source == JNI_TRUE,
                                  .stage_source = stage_source == JNI_TRUE,
                                  .room_loaded = room_loaded == JNI_TRUE,
                                  .room_shown = room_shown == JNI_TRUE,
                                  .recording = recording == JNI_TRUE});
        jclass type = env->FindClass("com/dmcrengine/nativeviewer/CompanionMenu");
        if (type == nullptr) return nullptr;
        jmethodID make = env->GetMethodID(
            type, "<init>",
            "(Lcom/dmcrengine/nativeviewer/NativeMenu;Lcom/dmcrengine/nativeviewer/NativeMenu;"
            "Lcom/dmcrengine/nativeviewer/NativeMenu;[I)V");
        if (make == nullptr) return nullptr;
        stage_actions::Menu shortcuts{.title = {}, .message = {}, .items = menu.shortcuts};
        stage_actions::Menu rows{.title = {}, .message = {}, .items = menu.rows};
        stage_actions::Menu more{.title = {}, .message = {}, .items = menu.more};
        return env->NewObject(type, make, menu_object(env, shortcuts), menu_object(env, rows),
                              menu_object(env, more), int_array(env, menu.dimmed));
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_runCompanionAction(JNIEnv* env, jclass, jlong handle, jint id) {
    const SessionLock jni_lock{session_mutex()};
    std::unique_ptr<Session> opened;
    const auto outcome = dmcresource::spider::actions::run_companion_action(from_handle(handle), id, &opened);
    return outcome_object(env, outcome, opened ? to_handle(opened.release()) : 0);
}

// The picture a session exports: {width, height}, chosen by the core.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_exportImageSize(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto [width, height] = stage_actions::export_image_size(from_handle(handle));
    return int_array(env, {width, height});
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_renderExportImage(JNIEnv* env, jclass, jlong handle, jobject target) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return image_to_bitmap(env, target, stage_actions::render_export_image(from_handle(handle))) ? JNI_TRUE
                                                                                                     : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
}

// HITS sources of a stage scene session (its own collision view).
extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_stageCollisionSourceCount(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    return session != nullptr && session->stage != nullptr
               ? static_cast<jint>(session->stage->collision_sources.size())
               : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_clearRoom(JNIEnv*, jclass) {
    dmcresource::stage_room::set_current(nullptr);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomSpotCount(JNIEnv*, jclass) {
    const auto room = dmcresource::stage_room::current();
    return room ? static_cast<jint>(room->spots.size()) : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_nextRoomSpot(JNIEnv*, jclass) {
    dmcresource::stage_room::set_spot(dmcresource::stage_room::spot() + 1U);
    return static_cast<jint>(dmcresource::stage_room::spot());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomCollisionSourceCount(JNIEnv*, jclass) {
    const auto room = dmcresource::stage_room::current();
    return room ? static_cast<jint>(room->collision_sources.size()) : 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_isStageSession(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    return session != nullptr && dmcresource::stage_room::is_stage_session(*session) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_collisionAttackIds(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto* session = from_handle(handle);
        const auto ids = session != nullptr ? dmcresource::collision::collision_attack_ids(*session)
                                            : std::vector<int>{};
        auto out = env->NewIntArray(static_cast<jsize>(ids.size()));
        if (out != nullptr && !ids.empty()) {
            env->SetIntArrayRegion(out, 0, static_cast<jsize>(ids.size()),
                                   reinterpret_cast<const jint*>(ids.data()));
        }
        return out;
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_selectCollisionAttack(
        JNIEnv* env, jclass, jlong handle, jint attack) {
    const SessionLock jni_lock{session_mutex()};
    try {
        auto* session = from_handle(handle);
        if (!dmcresource::collision::select_collision_attack(session, attack)) return env->NewStringUTF("");
        return env->NewStringUTF(dmcresource::collision::describe_collision_selection(*session).c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_classEventNames(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto names = dmcresource::motion::class_event_names(from_handle(handle));
        jclass string_class = env->FindClass("java/lang/String");
        if (string_class == nullptr) return nullptr;
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(names.size()), string_class, nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t index = 0U; index < names.size(); ++index) {
            jstring value = env->NewStringUTF(names[index]);
            if (value == nullptr) return nullptr;
            env->SetObjectArrayElement(out, static_cast<jsize>(index), value);
            env->DeleteLocalRef(value);
        }
        return out;
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_triggerClassEvent(
        JNIEnv*, jclass, jlong handle, jint index) {
    const SessionLock jni_lock{session_mutex()};
    if (index < 0) return JNI_FALSE;
    return dmcresource::motion::trigger_class_event(from_handle(handle), static_cast<std::size_t>(index))
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionEndFrame(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::motion::motion_end_frame(from_handle(handle));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLoopStartFrame(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::motion::motion_loop_start_frame(from_handle(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setMotionFrame(
        JNIEnv*, jclass, jlong handle, jfloat frame) {
    const SessionLock jni_lock{session_mutex()};
    return dmcresource::motion::apply_motion_frame(from_handle(handle), frame)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_clearMotion(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    dmcresource::motion::clear_motion(from_handle(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_assemblePacs(
        JNIEnv* env, jclass, jlongArray handles, jobjectArray names, jint enemy_variant) {
    const SessionLock jni_lock{session_mutex()};
    if (handles == nullptr || names == nullptr) return 0;
    const jsize count = env->GetArrayLength(handles);
    if (count < 1 || env->GetArrayLength(names) != count) return 0;
    try {
        std::vector<jlong> raw(static_cast<std::size_t>(count));
        env->GetLongArrayRegion(handles, 0, count, raw.data());
        if (env->ExceptionCheck()) return 0;
        std::vector<const Session*> archives;
        std::vector<std::string> owned_names;
        for (jsize index = 0; index < count; ++index) {
            const Session* archive = from_handle(raw[static_cast<std::size_t>(index)]);
            if (archive == nullptr) return 0;
            archives.push_back(archive);
            jstring value = static_cast<jstring>(env->GetObjectArrayElement(names, index));
            if (env->ExceptionCheck()) return 0;
            owned_names.push_back(to_utf8(env, value));
            if (value != nullptr) env->DeleteLocalRef(value);
        }
        std::vector<std::string_view> views(owned_names.begin(), owned_names.end());
        return to_handle(dmcresource::pac_assembly::assemble_archives(
            archives, views, nullptr,
            static_cast<std::size_t>(enemy_variant < 0 ? 0 : enemy_variant)).release());
    } catch (...) { return 0; }
}

// Selectable positions of an archive (em000.pac enemy classes and weapons,
// em028.pac dress states); empty when the archive has only one look.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_archiveVariantNames(
        JNIEnv* env, jclass, jstring archive_name) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const auto name = to_utf8(env, archive_name);
        const auto variants = dmcresource::motion::archive_variants(name);
        jclass string_class = env->FindClass("java/lang/String");
        if (string_class == nullptr) return nullptr;
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(variants.size()),
                                               string_class, nullptr);
        if (out == nullptr) return nullptr;
        for (std::size_t index = 0U; index < variants.size(); ++index) {
            jstring value = env->NewStringUTF(variants[index].label.c_str());
            if (value == nullptr) return nullptr;
            env->SetObjectArrayElement(out, static_cast<jsize>(index), value);
            env->DeleteLocalRef(value);
        }
        return out;
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_nonCanonicalNotes(
        JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    try {
        std::string joined;
        for (const auto& note : session->non_canonical_notes) {
            if (!joined.empty()) joined += "\n";
            joined += "- " + note;
        }
        return env->NewStringUTF(joined.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

// ---- Texture format change (Spider texture re-encode action) ----

namespace {

std::string& last_reencode_detail() noexcept {
    static std::string detail;
    return detail;
}

jobjectArray to_string_array(JNIEnv* env, const std::vector<std::string>& values) {
    jclass string_class = env->FindClass("java/lang/String");
    if (string_class == nullptr) return nullptr;
    jobjectArray out = env->NewObjectArray(static_cast<jsize>(values.size()), string_class, nullptr);
    if (out == nullptr) return nullptr;
    for (std::size_t i = 0U; i < values.size(); ++i) {
        jstring s = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(out, static_cast<jsize>(i), s);
        if (s != nullptr) env->DeleteLocalRef(s);
    }
    return out;
}

}  // namespace

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_textureFormatNames(JNIEnv* env, jclass) {
    try {
        std::vector<std::string> names;
        for (const auto& c : dmcresource::spider::actions::texture_format_choices()) names.push_back(c.name);
        return to_string_array(env, names);
    } catch (...) { return nullptr; }
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_textureFormatLabels(JNIEnv* env, jclass) {
    try {
        std::vector<std::string> labels;
        for (const auto& c : dmcresource::spider::actions::texture_format_choices()) labels.push_back(c.label);
        return to_string_array(env, labels);
    } catch (...) { return nullptr; }
}

// Opens the re-encoded result as a new session (0 on failure; the reason,
// naming the failed Spider step, is in reencodeTexturesDetail()). A session
// opened from a PAC slot rebuilds its PAC natively (Black Widow
// ReencodeRebuildsContainer); the shell passes nothing about containers.
extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_reencodeTextures(
        JNIEnv* env, jclass, jlong handle, jstring format, jboolean force_dx10) {
    const SessionLock jni_lock{session_mutex()};
    try {
        const Session* target = from_handle(handle);
        std::string detail;
        auto result = dmcresource::spider::actions::reencode_textures(
            target, to_utf8(env, format), force_dx10 == JNI_TRUE, &detail);
        last_reencode_detail() = detail;
        return result ? to_handle(result.release()) : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_reencodeTexturesDetail(JNIEnv* env, jclass) {
    const SessionLock jni_lock{session_mutex()};
    try {
        return env->NewStringUTF(last_reencode_detail().c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_sourceFileName(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    try {
        return env->NewStringUTF(session != nullptr ? session->source_name.c_str() : "");
    } catch (...) { return env->NewStringUTF(""); }
}

// Writes the session's file (an authored result) to a writable descriptor.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_writeSource(JNIEnv*, jclass, jlong handle, jint fd) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr || !session->authored || session->source_bytes == nullptr || fd < 0) return JNI_FALSE;
    const auto& bytes = *session->source_bytes;
    std::size_t written = 0U;
    while (written < bytes.size()) {
        const auto n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n <= 0) return JNI_FALSE;
        written += static_cast<std::size_t>(n);
    }
    return ::ftruncate(fd, static_cast<off_t>(bytes.size())) == 0 || written == bytes.size() ? JNI_TRUE : JNI_FALSE;
}

// ---- Renderer: GPU (OpenGL ES 3 on the device's graphics chip) or CPU.

namespace {
// The backend is registered when the library loads; its EGL context is made
// on the first frame drawn.
const bool g_gles_backend_installed = (dmcviewer::install_gles_view_backend(), true);
}  // namespace

// The view's window surface the GPU shows frames on (null: none). Returns
// once the backend has let go of the one before.
extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setPresentSurface(JNIEnv* env, jclass, jobject surface) {
    (void)g_gles_backend_installed;
    auto* backend = dmcresource::gpu_view_backend();
    if (backend == nullptr) return;
    ANativeWindow* window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
    backend->set_window(window);
}

// Frames straight to the window (Settings -> Graphics), else read back.
extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setGpuPresent(JNIEnv*, jclass, jboolean enabled) {
    dmcresource::set_gpu_present_enabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setDmc3Shaders(JNIEnv*, jclass, jboolean enabled) {
    dmcresource::set_dmc3_shaders_enabled(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setGpuRendering(JNIEnv*, jclass, jboolean enabled) {
    dmcresource::set_gpu_view_enabled(enabled == JNI_TRUE);
}

// Graphics settings of the GPU pass: MSAA samples (0 = off), mipmaps,
// anisotropic filtering (1 = off).
extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setGpuOptions(JNIEnv*, jclass, jint msaa, jboolean mipmaps,
                                                            jint anisotropy) {
    dmcresource::set_gpu_view_options({static_cast<int>(msaa), mipmaps == JNI_TRUE, static_cast<int>(anisotropy)});
}

// The chip's limits: {max MSAA samples, max anisotropy}; zeros without a GPU.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_gpuCapabilities(JNIEnv* env, jclass) {
    jint values[2] = {0, 0};
    try {
        (void)g_gles_backend_installed;
        if (auto* backend = dmcresource::gpu_view_backend()) {
            const auto caps = backend->capabilities();
            values[0] = caps.max_samples;
            values[1] = static_cast<jint>(caps.max_anisotropy);
        }
    } catch (...) {}
    jintArray out = env->NewIntArray(2);
    if (out != nullptr) env->SetIntArrayRegion(out, 0, 2, values);
    return out;
}

// "GPU: OpenGL ES 3.2 / Adreno (TM) ...", "CPU (software)", ... plus the
// frame counts so far.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_rendererInfo(JNIEnv* env, jclass) {
    try {
        (void)g_gles_backend_installed;
        const auto stats = dmcresource::gpu_view_stats();
        const auto text = dmcresource::view_renderer_description() + "\nFrames: " +
            std::to_string(stats.gpu_frames) + " GPU (" + std::to_string(stats.presented_frames) +
            " shown straight on the screen), " + std::to_string(stats.cpu_frames) + " CPU" +
            (stats.gpu_failures != 0U ? ", " + std::to_string(stats.gpu_failures) + " GPU fallbacks" : "") +
            "\n" + dmcresource::dmc3_shader_report();
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

// ---- Frame buffers in native memory ------------------------------------------
//
// ByteBuffer.allocateDirect lives in the Java heap on Android (a non-movable
// array), whose limit two 8K frames (2 x ~120 MB) exceed, so the viewer kept
// falling back to 6K. These buffers come from the native heap instead; the
// view frees each one exactly once when it is no longer in use.

extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_allocateFrameBuffer(JNIEnv* env, jclass, jlong bytes) {
    if (bytes <= 0) return nullptr;
    void* memory = std::malloc(static_cast<std::size_t>(bytes));
    if (memory == nullptr) return nullptr;
    jobject buffer = env->NewDirectByteBuffer(memory, bytes);
    if (buffer == nullptr) std::free(memory);
    return buffer;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_freeFrameBuffer(JNIEnv* env, jclass, jobject buffer) {
    if (buffer != nullptr) std::free(env->GetDirectBufferAddress(buffer));
}

// ---- Benchmark ------------------------------------------------------------------

// The viewer's frame loop without the screen, as fast as it goes for
// `seconds`: pose the motion (motion_start NaN: no motion), render with the
// gesture controls, copy the pixels out like renderToBuffer. Returns the
// report (format_view_benchmark) with the renderer and `settings` in it.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_benchmarkView(
        JNIEnv* env, jclass, jlong handle, jint width, jint height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw, jboolean follow, jfloat dolly,
        jfloat motion_start, jfloat seconds, jstring settings) {
    dmcresource::ViewBenchmarkResult result;
    int out_width = 0, out_height = 0;
    try {
        std::vector<std::uint8_t> pixels;
        const auto controls = view_controls(pan_x, pan_y, room_yaw, follow, dolly);
        float end_frame = 0.0F, loop_start = 0.0F;
        {
            const SessionLock jni_lock{session_mutex()};
            const Session* session = from_handle(handle);
            if (session == nullptr) return env->NewStringUTF("Nothing to draw.");
            if (std::isfinite(motion_start)) {
                end_frame = dmcresource::motion::motion_end_frame(session);
                loop_start = dmcresource::motion::motion_loop_start_frame(session);
            }
        }
        dmcresource::ViewBenchmarkOptions options;
        options.seconds = std::clamp(static_cast<double>(seconds), 1.0, 120.0);
        dmcresource::frame_profile::reset();
        result = dmcresource::run_view_benchmark(options, [&](int index) {
            // The lock is taken per frame, as the render thread does, so the
            // UI thread is never kept waiting for the whole run.
            const SessionLock jni_lock{session_mutex()};
            Session* session = from_handle(handle);
            if (session == nullptr) return false;
            if (std::isfinite(motion_start)) {
                // One game frame (60 per second) per drawn frame, looping like the viewer.
                float frame = motion_start + static_cast<float>(index);
                if (end_frame > 0.0F && frame > end_frame) {
                    const float span = end_frame - loop_start;
                    frame = span > 0.0F ? loop_start + std::fmod(frame - end_frame, span) : end_frame;
                }
                const dmcresource::frame_profile::Scope timed{dmcresource::frame_profile::Stage::Pose};
                (void)dmcresource::motion::apply_motion_frame(session, frame);
            }
            // The frame the view draws: through the camera when camera mode is on.
            const auto image = dmcresource::spider::camera::render_view_frame(
                session, width, height, dmcresource::benchmark_yaw(yaw, index), pitch, zoom,
                static_cast<std::uint32_t>(render_flags), controls);
            if (image.pixels.empty()) return false;
            out_width = image.width;
            out_height = image.height;
            pixels.resize(image.pixels.size());
            {
                const dmcresource::frame_profile::Scope timed{dmcresource::frame_profile::Stage::CopyOut};
                std::memcpy(pixels.data(), image.pixels.data(), image.pixels.size());
            }
            dmcresource::frame_profile::frame_done();
            return true;
        });
        // Leave the pose where the run started.
        if (std::isfinite(motion_start)) {
            const SessionLock jni_lock{session_mutex()};
            if (Session* session = from_handle(handle)) (void)dmcresource::motion::apply_motion_frame(session, motion_start);
        }
    } catch (...) {
        result.stopped = true;
    }
    try {
        auto text = dmcresource::format_view_benchmark(
            result, dmcresource::view_renderer_description(), out_width, out_height, to_utf8(env, settings));
        // Where the time went, stage by stage (warm-up frames included).
        const auto stages = dmcresource::frame_profile::per_frame_line();
        if (!stages.empty()) text += "\n" + stages;
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF("Benchmark failed."); }
}

// ---- Fly camera ---------------------------------------------------------------

// Fly camera (camera_control.h): the core places the eye where the orbit
// camera stands, flies it, and on the way back leaves the orbit turning round
// the point it looked at. Java forwards fingers and stores the angles it gets.

namespace {
jfloatArray floats(JNIEnv* env, std::initializer_list<float> values) {
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(values.size()));
    if (out != nullptr) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(values.size()), values.begin());
    return out;
}
}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_enterFly(
        JNIEnv*, jclass, jlong handle, jint width, jint height, jfloat yaw, jfloat pitch, jfloat zoom,
        jint render_flags, jfloat pan_x, jfloat pan_y, jfloat room_yaw, jboolean follow, jfloat dolly) {
    try {
        const SessionLock jni_lock{session_mutex()};
        const auto eye = dmcresource::session_camera_eye(from_handle(handle), width, height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags), view_controls(pan_x, pan_y, room_yaw, follow, dolly));
        if (!eye) return JNI_FALSE;
        dmcresource::camera::enter_fly(*eye);
        return JNI_TRUE;
    } catch (...) { return JNI_FALSE; }
}

// The orbit pose after the flight: {yaw, pitch, pan x, pan y, dolly}.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_leaveFly(JNIEnv* env, jclass, jlong handle, jfloat yaw, jfloat pitch) {
    const SessionLock jni_lock{session_mutex()};
    dmcresource::camera::leave_fly(yaw, pitch, dmcresource::session_camera_distance(from_handle(handle)));
    return floats(env, {yaw, pitch, 0.0F, 0.0F, 0.0F});
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_resetCamera(JNIEnv*, jclass) {
    dmcresource::camera::reset();
}

// One frame of flying from the two sticks (finger offsets from their bases in
// pixels, held or not): {yaw, pitch, move boost, move zone}.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_flyTick(
        JNIEnv* env, jclass, jlong handle, jfloat yaw, jfloat pitch, jboolean move_held, jfloat move_dx,
        jfloat move_dy, jboolean look_held, jfloat look_dx, jfloat look_dy, jfloat density, jfloat dt) {
    namespace camera = dmcresource::camera;
    const auto move = move_held == JNI_TRUE ? camera::stick_response(move_dx, move_dy, density)
                                            : camera::StickResponse{};
    const auto look = look_held == JNI_TRUE ? camera::stick_response(look_dx, look_dy, density)
                                            : camera::StickResponse{};
    float speed = 1.0F;
    {
        const SessionLock jni_lock{session_mutex()};
        // Full stick crosses the framed distance in about two seconds.
        speed = std::max(1.0F, dmcresource::session_camera_distance(from_handle(handle)) * 0.5F);
    }
    const auto step = camera::fly_tick(yaw, pitch, move, look, dt, speed);
    return floats(env, {step.yaw, step.pitch, move.boost, static_cast<float>(move.zone)});
}

// A cutscene's camera taken by a gesture (demo_session.h): {yaw, pitch,
// zoom} of the shot it stood at, its eye becoming the free camera's; null
// when the cuts were not driving the view.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoTakeCamera(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto view = dmcresource::demo::take_camera(from_handle(handle));
    if (!view) return nullptr;
    dmcresource::camera::enter_fly(view->eye);
    return floats(env, {view->yaw, view->pitch, view->zoom});
}

// A stage's start view (spider stage_actions.h): the core reframes the
// session on its spawn point's game camera or its floor; {yaw, pitch, zoom}.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_stageStartView(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto view = dmcresource::spider::actions::stage_start_view(from_handle(handle));
    if (!view) return nullptr;
    return floats(env, {(*view)[0], (*view)[1], (*view)[2]});
}

// A cutscene's pad vibration at a frame, 0..255 (core demo::vibration).
extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoVibration(JNIEnv*, jclass, jlong handle, jfloat frame) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    return session != nullptr ? dmcresource::demo::vibration(*session, frame) : 0;
}

// A cutscene's subtitle at its current frame (core demo::subtitle): its
// lines joined by newlines, null when none shows.
extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoSubtitle(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    const auto shown = session != nullptr ? dmcresource::demo::subtitle(*session) : std::nullopt;
    if (!shown) return nullptr;
    std::string text;
    for (const auto& line : shown->lines) {
        if (!text.empty()) text.push_back('\n');
        text += line;
    }
    return env->NewStringUTF(text.c_str());
}

// How to draw it (messages::layout): {centre x, top, size} as fractions of
// the picture, alpha, front r g b, back r g b (0..255); null when none shows.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoSubtitleStyle(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    const auto shown = session != nullptr ? dmcresource::demo::subtitle(*session) : std::nullopt;
    if (!shown) return nullptr;
    const auto l = dmcresource::messages::layout(*shown);
    const auto f = [](std::uint8_t v) { return static_cast<float>(v); };
    return floats(env, {l.centre_x, l.top, l.size, l.alpha, f(l.front[0]), f(l.front[1]), f(l.front[2]),
                        f(l.back[0]), f(l.back[1]), f(l.back[2])});
}

// A cutscene's sound (spider companion_menu.h): the picked .ogg as a
// descriptor; the outcome carries the core's line.
extern "C" JNIEXPORT jobject JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadCutsceneSound(JNIEnv* env, jclass, jlong handle, jint fd,
                                                                jstring filename) {
    dmcresource::spider::actions::ActionOutcome outcome;
    try {
        const auto name = to_utf8(env, filename);
        ReadOnlyMap mapped(fd);
        const SessionLock jni_lock{session_mutex()};
        outcome = dmcresource::spider::actions::load_cutscene_sound(
            from_handle(handle), name, fd >= 0 && mapped.valid() ? mapped.data() : nullptr,
            fd >= 0 && mapped.valid() ? mapped.size() : 0U);
    } catch (...) {
    }
    return outcome_object(env, outcome);
}

// The cutscene sound mix the shell streams: {rate, channels}; null without.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoAudioFormat(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto format = dmcresource::demo::audio_format(from_handle(handle));
    if (!format) return nullptr;
    return int_array(env, {static_cast<int>(format->sample_rate), static_cast<int>(format->channels)});
}

// The mix's sample frame at a timeline frame.
extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoAudioPosition(JNIEnv*, jclass, jlong handle, jfloat frame) {
    const SessionLock jni_lock{session_mutex()};
    return static_cast<jlong>(dmcresource::demo::audio_position(from_handle(handle), frame));
}

// `frames` sample frames of the mix from `position` (interleaved); null at
// the end.
extern "C" JNIEXPORT jshortArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_demoAudioRead(JNIEnv* env, jclass, jlong handle, jlong position,
                                                            jint frames) {
    try {
        std::vector<std::int16_t> samples;
        {
            const SessionLock jni_lock{session_mutex()};
            samples = dmcresource::demo::audio_read(from_handle(handle), static_cast<std::int64_t>(position),
                                                    frames > 0 ? static_cast<std::size_t>(frames) : 0U);
        }
        if (samples.empty()) return nullptr;
        jshortArray out = env->NewShortArray(static_cast<jsize>(samples.size()));
        if (out != nullptr) env->SetShortArrayRegion(out, 0, static_cast<jsize>(samples.size()), samples.data());
        return out;
    } catch (...) { return nullptr; }
}

// Where the HUD draws a stick's knob for a finger offset (px): {dx, dy}.
extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_stickKnob(JNIEnv* env, jclass, jfloat dx, jfloat dy, jfloat density) {
    const auto knob = dmcresource::camera::stick_knob(dx, dy, density);
    return floats(env, {knob.first, knob.second});
}

// One gesture per touch (core camera_control): dolly 2, turn 3 ask for it.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_claimGesture(JNIEnv*, jclass, jint gesture) {
    if (gesture < 0 || gesture > 3) return JNI_FALSE;
    return dmcresource::camera::claim_gesture(static_cast<dmcresource::camera::Gesture>(gesture)) ? JNI_TRUE
                                                                                                    : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_zoomStep(JNIEnv*, jclass, jfloat touch_zoom, jfloat next,
                                                       jboolean settling) {
    return dmcresource::camera::zoom_step(touch_zoom, next, settling == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_endTouch(JNIEnv*, jclass) {
    dmcresource::camera::end_touch();
}

