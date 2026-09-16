#include "dmcresource/spider/session_actions.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "dmcresource/spider/crusader.h"
#include "dmcresource/texture_companion.h"

namespace dmcresource::spider::actions {
namespace {

namespace crusader = dmcresource::spider::crusader;
constexpr crusader::OperationId kAttachPtx = 2U;
constexpr int kWholeSession = -1;

[[nodiscard]] bool has_required_slots(const CompositePart& part) noexcept {
    return std::any_of(
        part.render_triangle_texture_slots.begin(),
        part.render_triangle_texture_slots.end(),
        [](std::uint32_t slot) { return slot != kNoTextureSlot; });
}

void refresh_attachment_completion(Session* session) noexcept {
    if (session == nullptr || session->composite_parts.empty()) return;
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

[[nodiscard]] bool compact_staged_textures(
    std::vector<ImagePreview>* textures,
    std::vector<std::uint32_t>* slots) noexcept {
    if (textures == nullptr || slots == nullptr) return false;
    if (textures->empty()) return true;
    try {
        std::vector<bool> referenced(textures->size(), false);
        for (const auto slot : *slots) {
            if (slot == kNoTextureSlot) continue;
            if (slot >= textures->size()) return false;
            referenced[slot] = true;
        }

        std::vector<std::uint32_t> remap(textures->size(), kNoTextureSlot);
        std::vector<ImagePreview> compacted;
        compacted.reserve(textures->size());
        for (std::size_t index = 0U; index < textures->size(); ++index) {
            if (!referenced[index]) continue;
            if (compacted.size() >= static_cast<std::size_t>(kNoTextureSlot)) {
                return false;
            }
            remap[index] = static_cast<std::uint32_t>(compacted.size());
            compacted.push_back(std::move((*textures)[index]));
        }

        for (auto& slot : *slots) {
            if (slot == kNoTextureSlot) continue;
            const auto mapped = remap[slot];
            if (mapped == kNoTextureSlot) return false;
            slot = mapped;
        }
        *textures = std::move(compacted);
        return true;
    } catch (...) {
        return false;
    }
}

// Allocating helpers intentionally propagate exceptions to attach_operation,
// the Crusader OperationFn noexcept boundary. That keeps transactional PTX
// logic unchanged while preventing diagnostic/allocation failure from calling
// std::terminate.
[[nodiscard]] bool attach_single(Session* session,
                                 std::string_view name,
                                 const std::uint8_t* bytes,
                                 std::size_t size) {
    if (session == nullptr) return false;
    auto attachment = texture_companion::attach_ptx(
        name, bytes, size,
        {
            .mesh = &session->render_mesh,
            .triangle_texture_slots = session->render_triangle_texture_slots,
        });
    if (!attachment.attached) {
        session->texture_attachment_detail = attachment.detail;
        return false;
    }

    session->attached_textures = std::move(attachment.textures);
    session->texture_companion_attached = true;
    session->texture_attachment_detail = attachment.detail +
        " | action=spider.crusader";
    return true;
}

[[nodiscard]] bool attach_shared(Session* session,
                                 std::string_view name,
                                 const std::uint8_t* bytes,
                                 std::size_t size) {
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

    // Atomic ownership switch after parse/decode/projection validation.
    session->attached_textures = std::move(attachment.textures);
    session->render_triangle_texture_slots = std::move(shared_slots);
    for (auto& part : session->composite_parts) {
        if (!has_required_slots(part)) continue;
        part.texture_companion_attached = true;
        part.texture_attachment_detail = "shared bank: " + std::string{name};
    }
    refresh_attachment_completion(session);
    session->texture_attachment_detail = attachment.detail +
        " | compositeParts=" + std::to_string(views.size()) +
        " | duplicatePartRgba=0 | slotIdentityPreserved=1"
        " | action=spider.crusader";
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
                               std::size_t size) {
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

    // Fully transactional replacement: all slot and texture mutations are
    // staged. The previous valid bank remains untouched on every failure path.
    std::vector<std::uint32_t> staged_slots;
    std::vector<ImagePreview> staged_textures;
    std::vector<std::uint32_t> local_to_texture;
    std::vector<std::uint32_t> existing_by_local;
    std::vector<bool> existing_conflict;
    try {
        staged_slots = session->render_triangle_texture_slots;
        staged_textures = session->attached_textures;
        local_to_texture.assign(attachment.textures.size(), kNoTextureSlot);
        existing_by_local.assign(attachment.textures.size(), kNoTextureSlot);
        existing_conflict.assign(attachment.textures.size(), false);

        for (std::size_t local_triangle = 0U;
             local_triangle < part.render_triangle_texture_slots.size();
             ++local_triangle) {
            const auto local_slot = part.render_triangle_texture_slots[local_triangle];
            if (local_slot == kNoTextureSlot) continue;
            if (local_slot >= existing_by_local.size()) {
                session->texture_attachment_detail =
                    part.name + ": PTX rejected: local slot exceeds decoded bank";
                return false;
            }
            const auto current = session->render_triangle_texture_slots[begin + local_triangle];
            if (current == kNoTextureSlot || current >= session->attached_textures.size()) {
                continue;
            }
            if (existing_by_local[local_slot] == kNoTextureSlot) {
                existing_by_local[local_slot] = current;
            } else if (existing_by_local[local_slot] != current) {
                existing_conflict[local_slot] = true;
            }
        }

        for (std::size_t local = 0U; local < attachment.textures.size(); ++local) {
            auto& texture = attachment.textures[local];
            if (!texture.available()) continue;
            const auto existing = existing_by_local[local];
            if (!existing_conflict[local] && existing != kNoTextureSlot &&
                existing < session->attached_textures.size() &&
                image_equal(session->attached_textures[existing], texture)) {
                local_to_texture[local] = existing;
                continue;
            }
            if (staged_textures.size() >= static_cast<std::size_t>(kNoTextureSlot)) {
                session->texture_attachment_detail =
                    part.name + ": PTX rejected: texture index namespace exhausted";
                return false;
            }
            local_to_texture[local] = static_cast<std::uint32_t>(staged_textures.size());
            staged_textures.push_back(std::move(texture));
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

        if (!compact_staged_textures(&staged_textures, &staged_slots)) {
            session->texture_attachment_detail =
                part.name + ": PTX rejected: staged texture compaction failed";
            return false;
        }
    } catch (...) {
        // This helper is throwing-capable; the OperationFn boundary will turn a
        // diagnostic allocation failure here into a normal false result.
        try {
            session->texture_attachment_detail =
                part.name + ": PTX rejected: texture binding allocation failed";
        } catch (...) {
        }
        return false;
    }

    session->attached_textures = std::move(staged_textures);
    session->render_triangle_texture_slots = std::move(staged_slots);
    part.texture_companion_attached = true;
    part.texture_attachment_detail = attachment.detail;
    refresh_attachment_completion(session);
    session->texture_attachment_detail = part.name + ": " + attachment.detail +
        " | transactional=1 | duplicatePartRgba=0 | slotIdentityPreserved=1"
        " | action=spider.crusader";
    return true;
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
    try {
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
    } catch (...) {
        state->result = false;
        return false;
    }
}

const crusader::Plan& attach_plan() {
    static const crusader::Plan plan = [] {
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
    return plan;
}

}  // namespace

bool attach_ptx(
    Session* session,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    try {
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
        // attach_plan() may allocate its NativePlan on first use. Keep it inside
        // the public noexcept product boundary so failure stays fail-closed.
        const auto report = crusader::execute(attach_plan(), bindings, &state);
        return report.ok() && state.result;
    } catch (...) {
        return false;
    }
}

bool attach_ptx_to_part(
    Session* session,
    int part_index,
    std::string_view name,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    try {
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
        const auto report = crusader::execute(attach_plan(), bindings, &state);
        return report.ok() && state.result;
    } catch (...) {
        return false;
    }
}

}  // namespace dmcresource::spider::actions
