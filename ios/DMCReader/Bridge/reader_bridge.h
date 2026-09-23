#pragma once

// Portable C++23 boundary between the iOS shell and DMCNativeReader::Core.
//
// This is the iOS counterpart of app/src/main/cpp/app_native.cpp: it owns no
// parsing, capability or composition policy, only transport. It deliberately
// includes no Apple headers so it can be compiled and tested on any host
// against the real Core (see ios/tests/reader_bridge_test.cpp); the
// Objective-C++ wrapper in DmcBridge.mm is then a pure type conversion layer.
//
// Like JNI, this is a final catch-all: no C++ exception may cross it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dmcresource {
struct Session;
}

namespace dmc_ios {

struct Pixels {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba;  // RGBA8888, row-major, tightly packed
};

class ReaderSession {
public:
    ~ReaderSession();
    ReaderSession(const ReaderSession&) = delete;
    ReaderSession& operator=(const ReaderSession&) = delete;

    // Returns nullptr when the bytes are rejected: unsupported format, failed
    // structural validation, over the Core size cap, or any internal failure.
    [[nodiscard]] static std::unique_ptr<ReaderSession> open(
        std::string_view name, const std::uint8_t* bytes, std::size_t size) noexcept;

    // Black Widow capability/state bits, evaluated natively. The shell reads
    // them; it never reconstructs policy from file names or formats.
    [[nodiscard]] std::uint64_t state() const noexcept;

    [[nodiscard]] std::string describe() const noexcept;
    [[nodiscard]] std::string inspection() const noexcept;

    // Renders into `out`. `flags` is a dmcresource::RenderFlags mask.
    [[nodiscard]] bool render(int width, int height, float yaw, float pitch,
                              float zoom, std::uint32_t flags,
                              Pixels* out) const noexcept;

    // Decoded image of an image resource (DDS/TM2), when the session has one.
    [[nodiscard]] bool image_preview(Pixels* out) const noexcept;

    // Typed children published by container resources (e.g. PTX banks).
    [[nodiscard]] std::size_t child_count() const noexcept;
    [[nodiscard]] std::string child_title(int index) const noexcept;
    [[nodiscard]] bool child_preview(int index, Pixels* out) const noexcept;
    [[nodiscard]] std::unique_ptr<ReaderSession> open_child(int index) const noexcept;

    // Binds a PTX/DDS texture companion to the open model. The outcome, success
    // or the reason for rejection, is reported by texture_attachment_detail().
    [[nodiscard]] bool attach_texture(std::string_view name,
                                      const std::uint8_t* bytes,
                                      std::size_t size) noexcept;
    [[nodiscard]] std::string texture_attachment_detail() const noexcept;

private:
    explicit ReaderSession(std::unique_ptr<dmcresource::Session> session) noexcept;

    std::unique_ptr<dmcresource::Session> session_;
};

// Black Widow bits the iOS shell consumes. Values are taken from
// dmcresource::spider::black_widow::StateFlag in reader_bridge.cpp and checked
// there with static_assert, so they cannot drift from the native definition.
namespace state {
inline constexpr std::uint64_t kCanRender = 1ULL << 0U;
inline constexpr std::uint64_t kCanWireframe = 1ULL << 1U;
inline constexpr std::uint64_t kCanPreviewImage = 1ULL << 7U;
inline constexpr std::uint64_t kChildBrowserMode = 1ULL << 15U;
inline constexpr std::uint64_t kTextureCompanionAttachable = 1ULL << 16U;
inline constexpr std::uint64_t kTextureCompanionAttached = 1ULL << 17U;
}  // namespace state

// dmcresource::RenderFlag values used by the shell; checked the same way.
namespace render_flag {
inline constexpr std::uint32_t kWireframe = 1U << 0U;
}  // namespace render_flag

}  // namespace dmc_ios
