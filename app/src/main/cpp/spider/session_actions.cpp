#include "dmcresource/spider/session_actions.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/spider/crusader.h"
#include "dmcresource/texture_companion.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;

constexpr crusader::OperationId kComposeMods = 1U;
constexpr crusader::OperationId kAttachPtx = 2U;
constexpr int kWholeSession = -1;

[[nodiscard]] bool has_required_slots(const CompositePart& part) noexcept {
    return std::any_of(
        part.render_triangle_texture_slots.begin(),
        part.render_triangle_texture_slots.end(),
        [](std::uint32_t slot) { return slot != kNoTextureSlot; });
}

void refresh_attachment_completion(Session* session) noexcept {
    if (session == nullptr) return;
    if (session->composite_parts.empty()) return;

    bool any_required = false;
    bool complete = true;
    for (const auto& part : session->composite_parts) {
        if (!has_required_slots(part)) continue;
        any_required = true;
        complete = complete && part.texture_companion_attached;
    }
    session->texture_companion_attached = any_required && complete;
}

[[nodiscard]] bool build_shared_views(
    const Session& session,
    std::vector<texture_companion::ModelTextureView>* views) noexcept {
    if (views == nullptr) return false;
    views->clear();
    try {
        views->reserve(session.composite_parts.size());
        for (const auto& part : session.composite_parts) {
            if (!has_required_slots(part)) continue;
            views->push_back({
                .scene = &part.scene,
                .triangle_texture_slots = part.render_triangle_texture_slots,
            });
        }
    } catch (...) {
        views->clear();
        return false;
    }
    return !views->empty();
}

[[nodiscard]] bool build_shared_render_slots(
    const Session& session,
    std::vector<std::uint32_t>* slots) noexcept {
    if (slots == nullptr) return false;
    slots->clear();

    const std::size_t triangles = session.render_mesh.indices.size() / 3U;
    try {
        slots->reserve(triangles);
        for (const auto& part : session.composite_parts) {
            slots->insert(
                slots->end(),
                part.render_triangle_texture_slots.begin(),
                part.render_triangle_texture_slots.end());
        }
    } catch (...) {
        slots->clear();
        return false;
    }
    return slots->size() == triangles;
}

[[nodiscard]] bool image_equal(const ImagePreview& a,
                               const ImagePreview& b) noexcept {
    return a.width == b.width && a.height == b.height && a.rgba8 == b.rgba8;
}

[[nodiscard]] std::uint32_t find_identical_texture(
    const std::vector<ImagePreview>& textures,
    const ImagePreview& candidate) noexcept {
    if (!candidate.available()) return kNoTextureSlot;
    for (std::size_t index = 0U; index < textures.size(); ++index) {
        if (!textures[index].available()) continue;
        if (image_equal(textures[index], candidate)) {
            if (index >= static_cast<std::size_t>(kNoTextureSlot)) {
                return kNoTextureSlot;
            }
            return static_cast<std::uint32_t>(index);
        }
    }
    return kNoTextureSlot;
}

[[nodiscard]] bool compact_unreferenced_textures(Session* session) noexcept {
    if (session == nullptr || session->attached_textures.empty()) return true;

    const std::size_t old_count = session->attached_textures.size();
    try {
        std::vector<bool> referenced(old_count, false);
        for (const auto slot : session->render_triangle_texture_slots) {
            if (slot == kNoTextureSlot) continue;
            if (slot >= old_count) return false;
            referenced[slot] = true;
        }

        std::size_t referenced_count = 0U;
        for (const bool used : referenced) if (used) ++referenced_count;

        std::vector<std::uint32_t> remap(old_count, kNoTextureSlot);
        std::vector<ImagePreview> compacted;
        compacted.reserve(referenced_count);
        for (std::size_t index = 0U; index < old_count; ++index) {
            if (!referenced[index]) continue;
            if (compacted.size() >= static_cast<std::size_t>(kNoTextureSlot)) {
                return false;
            }
            remap[index] = static_cast<std::uint32_t>(compacted.size());
            compacted.push_back(std::move(session->attached_textures[index]));
        }

        for (auto& slot : session->render_triangle_texture_slots) {
            if (slot == kNoTextureSlot) continue;
            if (slot >= remap.size() || remap[slot] == kNoTextureSlot) return false;
            slot = remap[slot];
        }
        session->attached_textures = std::move(compacted);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool attach_single(Session* session,
                                 std::string_view name,
                                 const std::uint8_t* bytes,
                                 std::size_t size) noexcept {
    if (session == nullptr) return false;
    auto attachment = texture_companion::attach_ptx(
        name, bytes, size,
        {
            .mesh = &session->render_mesh,
            .triangle_texture_slots = session->render_triangle_texture_slots,
        });
    session->texture_attachment_detail = attachment.detail;
    if (!attachment.attached) return false;

    session->attached_textures = std::move(attachment.textures);
    session->texture_companion_attached = true;
    session->texture_attachment_detail += " | action=spider.crusader";
    return true;
}

[[nodiscard]] bool attach_shared(Session* session,
                                 std::string_view name,
                                 const std::uint8_t* bytes,
                                 std::size_t size) noexcept {
    if (session == nullptr || session->composite_parts.empty()) return false;

    std::vector<texture_companion::ModelTextureView> views;
    if (!build_shared_views(*session, &views)) {
        session->texture_attachment_detail =
            "Shared PTX rejected: composite has no attachable texture parts";
        return false;
    }

    auto attachment = texture_companion::attach_shared_ptx(
        name, bytes, size, views);
    if (!attachment.attached) {
        session->texture_attachment_detail = attachment.detail;
        return false;
    }

    std::vector<std::uint32_t> shared_slots;
    if (!build_shared_render_slots(*session, &shared_slots)) {
        session->texture_attachment_detail =
            "Shared PTX rejected: composite triangle projection is inconsistent";
        return false;
    }

    for (const auto slot : shared_slots) {
        if (slot == kNoTextureSlot) continue;
        if (slot >= attachment.textures.size() ||
            !attachment.textures[slot].available()) {
            session->texture_attachment_detail =
                "Shared PTX rejected: required local slot was not decoded";
            return false;
        }
    }

    // Atomic ownership switch: the previous bank/slot projection remains live
    // until all parsing, decoding and projection validation above has succeeded.
    session->attached_textures = std::move(attachment.textures);
    session->render_triangle_texture_slots = std::move(shared_slots);
    for (auto& part : session->composite_parts) {
        if (!has_required_slots(part)) continue;
        part.texture_companion_attached = true;
        part.texture_attachment_detail =
            "shared bank: " + std::string{name};
    }
    refresh_attachment_completion(session);
    session->texture_attachment_detail = attachment.detail +
        " | compositeParts=" + std::to_string(views.size()) +
        " | duplicateRgba=0 | action=spider.crusader";
    return session->texture_companion_attached;
}

[[nodiscard]] std::size_t part_triangle_begin(
    const Session& session,
    std::size_t part_index) noexcept {
    std::size_t offset = 0U;
    for (std::size_t index = 0U; index < part_index; ++index) {
        const auto count = session.composite_parts[index].render_triangle_texture_slots.size();
        if (count > std::numeric_limits<std::size_t>::max() - offset) {
            return std::numeric_limits<std::size_t>::max();
        }
        offset += count;
    }
    return offset;
}

[[nodiscard]] bool attach_part(Session* session,
                               int part_index,
                               std::string_view name,
                               const std::uint8_t* bytes,
                               std::size_t size) noexcept {
    if (session == nullptr || part_index < 0 ||
        static_cast<std::size_t>(part_index) >= session->composite_parts.size()) {
        return false;
    }

    auto& part = session->composite_parts[static_cast<std::size_t>(part_index)];
    auto attachment = texture_companion::attach_ptx(
        name, bytes, size,
        {
            .scene = &part.scene,
            .triangle_texture_slots = part.render_triangle_texture_slots,
        });
    if (!attachment.attached) {
        session->texture_attachment_detail = part.name + ": " + attachment.detail;
        return false;
    }

    const auto begin = part_triangle_begin(*session, static_cast<std::size_t>(part_index));
    if (begin == std::numeric_limits<std::size_t>::max() ||
        begin > session->render_triangle_texture_slots.size() ||
        part.render_triangle_texture_slots.size() >
            session->render_triangle_texture_slots.size() - begin) {
        session->texture_attachment_detail =
            part.name + ": PTX rejected: composite triangle range is inconsistent";
        return false;
    }

    std::vector<std::uint32_t> staged_slots;
    std::vector<std::uint32_t> local_to_texture;
    try {
        staged_slots = session->render_triangle_texture_slots;
        local_to_texture.assign(attachment.textures.size(), kNoTextureSlot);

        std::size_t new_texture_count = 0U;
        for (const auto& texture : attachment.textures) {
            if (texture.available() &&
                find_identical_texture(session->attached_textures, texture) == kNoTextureSlot) {
                ++new_texture_count;
            }
        }
        session->attached_textures.reserve(
            session->attached_textures.size() + new_texture_count);

        for (std::size_t local = 0U; local < attachment.textures.size(); ++local) {
            auto& texture = attachment.textures[local];
            if (!texture.available()) continue;
            auto actual = find_identical_texture(session->attached_textures, texture);
            if (actual == kNoTextureSlot) {
                if (session->attached_textures.size() >=
                    static_cast<std::size_t>(kNoTextureSlot)) {
                    session->texture_attachment_detail =
                        part.name + ": PTX rejected: texture index namespace exhausted";
                    return false;
                }
                actual = static_cast<std::uint32_t>(session->attached_textures.size());
                session->attached_textures.push_back(std::move(texture));
            }
            local_to_texture[local] = actual;
        }

        for (std::size_t local_triangle = 0U;
             local_triangle < part.render_triangle_texture_slots.size();
             ++local_triangle) {
            const auto local_slot = part.render_triangle_texture_slots[local_triangle];
            if (local_slot == kNoTextureSlot) {
                staged_slots[begin + local_triangle] = kNoTextureSlot;
                continue;
            }
            if (local_slot >= local_to_texture.size() ||
                local_to_texture[local_slot] == kNoTextureSlot) {
                session->texture_attachment_detail =
                    part.name + ": PTX rejected: required local slot was not decoded";
                return false;
            }
            staged_slots[begin + local_triangle] = local_to_texture[local_slot];
        }
    } catch (...) {
        session->texture_attachment_detail =
            part.name + ": PTX rejected: texture binding allocation failed";
        return false;
    }

    session->render_triangle_texture_slots = std::move(staged_slots);
    part.texture_companion_attached = true;
    part.texture_attachment_detail = attachment.detail;
    refresh_attachment_completion(session);

    const bool compacted = compact_unreferenced_textures(session);
    session->texture_attachment_detail = part.name + ": " + attachment.detail +
        " | duplicateRgba=0 | action=spider.crusader";
    if (!compacted) {
        session->texture_attachment_detail +=
            " | warning=texture-compaction-skipped";
    }
    return true;
}

struct ComposeState final {
    const std::vector<const Session*>* parts{};
    const std::vector<std::string>* names{};
    std::unique_ptr<Session> result;
};

bool compose_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<ComposeState*>(raw);
    if (state == nullptr || state->parts == nullptr || state->names == nullptr) {
        return false;
    }
    try {
        state->result = dmcresource::compose_mod_sessions(*state->parts, *state->names);
        if (!state->result) return false;
        if (!state->result->trace.empty()) state->result->trace += "\n";
        state->result->trace += "[OK] spider.crusader.action.compose-mods";
        return true;
    } catch (...) {
        state->result.reset();
        return false;
    }
}

struct AttachState final {
    Session* session{};
    std::string_view name;
    const std::uint8_t* bytes{};
    std::size_t size{};
    int part_index{kWholeSession};
    bool result{};
};

bool attach_operation(void* raw, std::uint32_t) noexcept {
    auto* state = static_cast<AttachState*>(raw);
    if (state == nullptr || state->session == nullptr) return false;
    if (state->part_index >= 0) {
        state->result = attach_part(
            state->session, state->part_index,
            state->name, state->bytes, state->size);
    } else if (!state->session->composite_parts.empty()) {
        state->result = attach_shared(
            state->session, state->name, state->bytes, state->size);
    } else {
        state->result = attach_single(
            state->session, state->name, state->bytes, state->size);
    }
    return state->result;
}

const crusader::Plan& one_step_plan(crusader::OperationId operation) {
    static const crusader::Plan compose = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kComposeMods,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    static const crusader::Plan attach = [] {
        crusader::Plan out;
        out.instructions.push_back({
            .operation = kAttachPtx,
            .operand = 0U,
            .dependency_begin = 0U,
            .dependency_count = 0U,
            .domain = crusader::Domain::cpu,
        });
        return out;
    }();
    return operation == kComposeMods ? compose : attach;
}

}  // namespace

std::unique_ptr<Session> compose_mod_sessions(
    const std::vector<const Session*>& parts,
    const std::vector<std::string>& names) noexcept {
    ComposeState state{.parts = &parts, .names = &names};
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kComposeMods,
            .execute = &compose_operation,
        },
    };
    const auto report = crusader::execute(one_step_plan(kComposeMods), bindings, &state);
    if (!report.ok()) return nullptr;
    return std::move(state.result);
}

bool attach_ptx(
    Session* session,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    AttachState state{
        .session = session,
        .name = name,
        .bytes = bytes,
        .size = size,
        .part_index = kWholeSession,
    };
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kAttachPtx,
            .execute = &attach_operation,
        },
    };
    const auto report = crusader::execute(one_step_plan(kAttachPtx), bindings, &state);
    return report.ok() && state.result;
}

bool attach_ptx_to_part(
    Session* session,
    int part_index,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    AttachState state{
        .session = session,
        .name = name,
        .bytes = bytes,
        .size = size,
        .part_index = part_index,
    };
    static const std::array bindings{
        crusader::OperationBinding{
            .operation = kAttachPtx,
            .execute = &attach_operation,
        },
    };
    const auto report = crusader::execute(one_step_plan(kAttachPtx), bindings, &state);
    return report.ok() && state.result;
}

}  // namespace dmcresource::spider::actions
