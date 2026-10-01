#include <cmath>
#include <mutex>
#include <unordered_set>
#include "dmcresource/resource_limits.h"

#include <android/bitmap.h>
#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
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
#include "dmcresource/inspection_format.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/motion/enemy_effects.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/view_gpu.h"
#include "android/gles_view_backend.h"

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
        const dmcresource::ViewControls controls{pan_x, pan_y, room_yaw, follow == JNI_TRUE, dolly};
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
        jint status = 1;
        if (std::isfinite(motion_frame) &&
            !dmcresource::motion::apply_motion_frame(session, motion_frame)) {
            status = 2;
        }
        const dmcresource::ViewControls controls{pan_x, pan_y, room_yaw, follow == JNI_TRUE, dolly};
        const auto image = dmcresource::render_session(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags), controls);
        if (image.width != requested_width || image.height != requested_height) return 0;
        auto* out = static_cast<std::uint8_t*>(env->GetDirectBufferAddress(buffer));
        const auto capacity = env->GetDirectBufferCapacity(buffer);
        if (out == nullptr || capacity < 0 ||
            static_cast<std::size_t>(capacity) < image.pixels.size()) {
            return 0;
        }
        std::memcpy(out, image.pixels.data(), image.pixels.size());
        return status;
    } catch (...) { return 0; }
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
        const dmcresource::ViewControls controls{pan_x, pan_y, room_yaw, follow == JNI_TRUE, dolly};
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasShadows(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    // SHW hulls, or the mesh fallback for any renderable model (a stage
    // scene is the floor itself).
    return session != nullptr && session->stage == nullptr &&
                   (session->renderable || !session->shadow_bindings.empty())
               ? JNI_TRUE
               : JNI_FALSE;
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

// True when the room drawn with the session has scrolling textures (a stage's
// clouds): the viewer then redraws on a timer.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomAnimated(JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    const bool has_host = dmcresource::stage_room::effect_host() != nullptr;
    if (session->stage != nullptr) {
        const auto& stage = dmcresource::stage_room::shown(*session->stage);
        return !stage.uv_scrolls.empty() || (has_host && !stage.effects.empty()) ? JNI_TRUE : JNI_FALSE;
    }
    const auto room = dmcresource::stage_room::shown(dmcresource::stage_room::current());
    return room && (!room->uv_scrolls.empty() || (has_host && !room->effects.empty())) ? JNI_TRUE : JNI_FALSE;
}

// Break toggle (stage_room::set_broken): whether the stage scene or room drawn
// with the session has breakable layout objects, and switching it.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomBreakable(JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    if (session != nullptr && session->stage != nullptr) {
        return session->stage->broken != nullptr ? JNI_TRUE : JNI_FALSE;
    }
    const auto room = dmcresource::stage_room::current();
    return room && room->broken ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setRoomBroken(JNIEnv*, jclass, jboolean broken) {
    dmcresource::stage_room::set_broken(broken == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_roomBroken(JNIEnv*, jclass) {
    return dmcresource::stage_room::broken() ? JNI_TRUE : JNI_FALSE;
}

// Kinds (distinct flag values) of the HITS shown with the session: its own
// file, its stage scene, or the room around it. One string per kind:
// "flags|records|floors|walls|ceilings|rgb".
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_collisionKinds(JNIEnv* env, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const Session* session = from_handle(handle);
    std::vector<dmcresource::environment_collision::Kind> kinds;
    try {
        if (session != nullptr && session->hits != nullptr) {
            kinds = dmcresource::environment_collision::kinds(*session->hits);
        } else if (session != nullptr && session->stage != nullptr) {
            kinds = session->stage->collision_kinds;
        } else if (const auto room = dmcresource::stage_room::current()) {
            kinds = room->collision_kinds;
        }
        jclass string_class = env->FindClass("java/lang/String");
        jobjectArray out = env->NewObjectArray(static_cast<jsize>(kinds.size()), string_class, nullptr);
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const auto color = dmcresource::collision_kind_color(i);
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasCollision(
        JNIEnv*, jclass, jlong handle) {
    const SessionLock jni_lock{session_mutex()};
    const auto* session = from_handle(handle);
    return session != nullptr && session->collision != nullptr ? JNI_TRUE : JNI_FALSE;
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
            std::to_string(stats.gpu_frames) + " GPU, " + std::to_string(stats.cpu_frames) + " CPU" +
            (stats.gpu_failures != 0U ? ", " + std::to_string(stats.gpu_failures) + " GPU fallbacks" : "");
        return env->NewStringUTF(text.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}
