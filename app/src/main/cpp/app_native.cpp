#include "dmcresource/resource_limits.h"

#include <android/bitmap.h>
#include <jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dmcresource/collision_debug.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/motion/motion_player.h"
#include "dmcresource/pac_assembly.h"
#include "dmcresource/motion/part_attachment.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/session_actions.h"
#include "dmcresource/view_renderer.h"

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

Session* from_handle(jlong handle) noexcept {
    return reinterpret_cast<Session*>(static_cast<std::uintptr_t>(handle));
}

jlong to_handle(Session* session) noexcept {
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
    delete from_handle(handle);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_info(
        JNIEnv* env, jclass, jlong handle) {
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
    try {
        return static_cast<jlong>(black_widow_state(from_handle(handle)));
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartCount(
        JNIEnv*, jclass, jlong handle) {
    try {
        const auto count = dmcresource::session_composite_part_count(from_handle(handle));
        if (count > static_cast<std::size_t>(std::numeric_limits<jint>::max())) return 0;
        return static_cast<jint>(count);
    } catch (...) { return 0; }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_compositePartName(
        JNIEnv* env, jclass, jlong handle, jint index) {
    try {
        const auto name = dmcresource::session_composite_part_name(from_handle(handle), index);
        return env->NewStringUTF(name.c_str());
    } catch (...) { return env->NewStringUTF(""); }
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_imagePreview(
        JNIEnv* env, jclass, jlong handle, jobject target) {
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;
    return preview_to_bitmap(env, target, session->image_preview)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_attachPtx(
        JNIEnv* env, jclass, jlong handle, jint fd, jstring filename) {
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
    const Session* session = from_handle(handle);
    if (session == nullptr) return env->NewStringUTF("");
    return env->NewStringUTF(session->texture_attachment_detail.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourceCount(
        JNIEnv*, jclass, jlong handle) {
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
    try {
        const auto title = dmcresource::session_child_title(from_handle(handle), index);
        return env->NewStringUTF(title.c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_childResourcePreviewWidth(
        JNIEnv*, jclass, jlong handle, jint index) {
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
    const Session* session = from_handle(handle);
    if (session == nullptr) return JNI_FALSE;

    try {
        const auto image = dmcresource::render_session(
            session, requested_width, requested_height, yaw, pitch, zoom,
            static_cast<std::uint32_t>(render_flags));
        return image_to_bitmap(env, target, image) ? JNI_TRUE : JNI_FALSE;
    } catch (...) { return JNI_FALSE; }
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

// --- Read-only PAC assembly and MOT playback (v34) -------------------------
// Java supplies handles/file descriptors only; binding, evaluation, skinning
// and archive classification live in DMCNativeReader::Core. All calls happen
// on the UI thread that also renders, so a Session is never posed and drawn
// concurrently.

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_assemblePac(
        JNIEnv* env, jclass, jlong handle, jstring archive_name) {
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
    const Session* session = from_handle(handle);
    if (session == nullptr) return 0;
    const auto count = session->motion_library.size();
    return count > static_cast<std::size_t>(std::numeric_limits<jint>::max())
        ? std::numeric_limits<jint>::max() : static_cast<jint>(count);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLibraryName(
        JNIEnv* env, jclass, jlong handle, jint index) {
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

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadLibraryMotion(
        JNIEnv* env, jclass, jlong handle, jint index) {
    Session* session = from_handle(handle);
    if (session == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= session->motion_library.size()) {
        return env->NewStringUTF("Motion: invalid library index");
    }
    try {
        // Copy: load_motion() may clear/replace state that references the library.
        const auto payload = session->motion_library[static_cast<std::size_t>(index)];
        const auto report = dmcresource::motion::load_motion(
            session, payload.name, payload.bytes.data(), payload.bytes.size());
        return env->NewStringUTF(report.detail.c_str());
    } catch (...) { return env->NewStringUTF("Motion: load failed"); }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_loadMotion(
        JNIEnv* env, jclass, jlong handle, jint fd, jstring filename) {
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
    return dmcresource::motion::has_motion(from_handle(handle)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasShadows(
        JNIEnv*, jclass, jlong handle) {
    const auto* session = from_handle(handle);
    return session != nullptr && !session->shadow_bindings.empty() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_hasCollision(
        JNIEnv*, jclass, jlong handle) {
    const auto* session = from_handle(handle);
    return session != nullptr && session->collision != nullptr ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_collisionAttackIds(
        JNIEnv* env, jclass, jlong handle) {
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
    try {
        auto* session = from_handle(handle);
        if (!dmcresource::collision::select_collision_attack(session, attack)) return env->NewStringUTF("");
        return env->NewStringUTF(dmcresource::collision::describe_collision_selection(*session).c_str());
    } catch (...) { return env->NewStringUTF(""); }
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionEndFrame(
        JNIEnv*, jclass, jlong handle) {
    return dmcresource::motion::motion_end_frame(from_handle(handle));
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_motionLoopStartFrame(
        JNIEnv*, jclass, jlong handle) {
    return dmcresource::motion::motion_loop_start_frame(from_handle(handle));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_setMotionFrame(
        JNIEnv*, jclass, jlong handle, jfloat frame) {
    return dmcresource::motion::apply_motion_frame(from_handle(handle), frame)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_clearMotion(
        JNIEnv*, jclass, jlong handle) {
    dmcresource::motion::clear_motion(from_handle(handle));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_dmcrengine_nativeviewer_NativeBridge_assemblePacs(
        JNIEnv* env, jclass, jlongArray handles, jobjectArray names, jint enemy_variant) {
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
