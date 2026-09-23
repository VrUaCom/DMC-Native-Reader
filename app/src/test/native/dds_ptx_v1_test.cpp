#include "dmc_rengine/codecs/dds_bc.hpp"
#include "dmcresource/decode_pipeline.h"
#include "dmcresource/resource_capabilities.h"
#include "dmcresource/texture_set.h"
#include "dmcresource/texture_companion.h"
#include "dmcresource/view_renderer.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/spider/session_actions.h"

#include <cassert>
#include "dmcresource/ptx_framing_compat.h"
#include "dmc_rengine/profiles/dmc3/texture_slot_framing_compat.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace {

namespace dds_bc = dmc::rengine::codecs::dds_bc;
namespace texture_set = dmcresource::texture_set;

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value) {
    assert(offset + 2U <= bytes.size());
    bytes[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (std::size_t i = 0U; i < 4U; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(
            (value >> (i * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> make_dds(bool dxt5 = false,
                                   std::uint32_t mip_count = 3U) {
    const std::size_t block_bytes = dxt5 ? 16U : 8U;
    assert(mip_count >= 1U && mip_count <= 3U);
    std::vector<std::uint8_t> bytes(
        128U + block_bytes * static_cast<std::size_t>(mip_count), 0U);
    bytes[0] = 'D'; bytes[1] = 'D'; bytes[2] = 'S'; bytes[3] = ' ';
    put_u32(bytes, 4U, 124U);
    put_u32(bytes, 8U, 0x000A1007U);
    put_u32(bytes, 12U, 4U);
    put_u32(bytes, 16U, 4U);
    put_u32(bytes, 20U, static_cast<std::uint32_t>(block_bytes));
    put_u32(bytes, 28U, mip_count);
    put_u32(bytes, 76U, 32U);
    put_u32(bytes, 80U, 4U);
    bytes[84] = 'D'; bytes[85] = 'X'; bytes[86] = 'T';
    bytes[87] = dxt5 ? '5' : '1';
    put_u32(bytes, 108U, mip_count > 1U ? 0x00401008U : 0x00001000U);

    if (dxt5) {
        bytes[128U] = 255U;
        bytes[129U] = 0U;
        put_u16(bytes, 136U, 0xF800U);
        put_u16(bytes, 138U, 0x07E0U);
        put_u32(bytes, 140U, 0U);
    } else {
        put_u16(bytes, 128U, 0xF800U);
        put_u16(bytes, 130U, 0x07E0U);
        put_u32(bytes, 132U, 0U);
    }
    return bytes;
}

std::vector<std::uint8_t> make_descriptor(
    const std::vector<std::uint8_t>& dds,
    bool dxt5,
    std::uint32_t auxiliary_mode = 0U,
    std::uint32_t auxiliary_value = 0U) {
    constexpr std::uint32_t width = 4U;
    constexpr std::uint32_t height = 4U;
    constexpr std::uint32_t mip_count = 3U;
    const auto payload_size = static_cast<std::uint32_t>(dds.size() - 128U);

    std::vector<std::uint8_t> descriptor(0x70U, 0U);
    put_u32(descriptor, 0x08U,
            0x20000U | (mip_count << 8U) | (dxt5 ? 0x88U : 0x86U));
    put_u32(descriptor, 0x0CU, 0xAAE4U);
    put_u32(descriptor, 0x10U, (height << 16U) | width);
    put_u32(descriptor, 0x14U, 1U);
    put_u32(descriptor, 0x18U, width * (dxt5 ? 4U : 2U));
    put_u32(descriptor, 0x20U, 0x40U);
    put_u32(descriptor, 0x38U, payload_size);
    put_u32(descriptor, 0x3CU, auxiliary_mode);
    put_u32(descriptor, 0x40U, auxiliary_value);
    put_u32(descriptor, 0x44U, (height << 16U) | width);
    put_u32(descriptor, 0x48U,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(width)));
    put_u32(descriptor, 0x4CU,
            std::bit_cast<std::uint32_t>(1.0F / static_cast<float>(height)));
    put_u32(descriptor, 0x60U, dxt5 ? 4U : 0U);
    put_u32(descriptor, 0x64U, static_cast<std::uint32_t>(dds.size()));
    put_u32(descriptor, 0x68U, 8U);
    return descriptor;
}

std::vector<std::uint8_t> make_wrapped_dds(bool dxt5 = false) {
    const auto dds = make_dds(dxt5);
    const auto descriptor = make_descriptor(dds, dxt5);
    std::vector<std::uint8_t> bytes(descriptor.size() + dds.size(), 0U);
    std::memcpy(bytes.data(), descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + descriptor.size(), dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_zero_final_span(
    bool dxt5 = false,
    std::uint32_t auxiliary_mode = 0U,
    std::uint32_t auxiliary_value = 0U) {
    const auto dds = make_dds(dxt5);
    const auto descriptor = make_descriptor(
        dds, dxt5, auxiliary_mode, auxiliary_value);
    std::vector<std::uint8_t> bytes(0x800U + descriptor.size() + dds.size(), 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 0U);
    std::memcpy(bytes.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

std::vector<std::uint8_t> make_ptx_sector_bounded() {
    const auto dds = make_dds(false);
    const auto descriptor = make_descriptor(dds, false);
    std::vector<std::uint8_t> bytes(0x1000U, 0U);
    put_u32(bytes, 0U, 1U);
    put_u32(bytes, 4U, 1U);
    std::memcpy(bytes.data() + 0x800U, descriptor.data(), descriptor.size());
    std::memcpy(bytes.data() + 0x870U, dds.data(), dds.size());
    return bytes;
}

std::span<const std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
    return std::as_bytes(std::span<const std::uint8_t>{bytes.data(), bytes.size()});
}

void require_red_preview(const dmcresource::ImagePreview& image) {
    assert(image.available());
    assert(image.width == 4U);
    assert(image.height == 4U);
    assert(image.rgba8.size() == 64U);
    for (std::size_t i = 0U; i < 16U; ++i) {
        const auto o = i * 4U;
        assert(image.rgba8[o + 0U] == 255U);
        assert(image.rgba8[o + 1U] == 0U);
        assert(image.rgba8[o + 2U] == 0U);
        assert(image.rgba8[o + 3U] == 255U);
    }
}

void require_texture_set_red(
    const texture_set::ParseResult& set,
    std::span<const std::byte> source,
    std::uint32_t slot_index = 0U) {
    assert(set.ok());
    const auto* slot = texture_set::find_slot(set, slot_index);
    assert(slot != nullptr);
    dmcresource::ImagePreview preview;
    std::string detail;
    assert(texture_set::decode_base_mip(source, *slot, &preview, &detail));
    require_red_preview(preview);
}

bool has_module(const dmcresource::PipelineResult& result, const char* name) {
    for (const auto& module : result.modules) {
        if (module.name != nullptr && std::strcmp(module.name, name) == 0) {
            return module.complete;
        }
    }
    return false;
}

}  // namespace

int main() {
    using dmcresource::ResourceCapability;
    using dmcresource::has_capability;
    using dmcresource::run_decode_pipeline;

    for (const bool dxt5 : {false, true}) {
        const auto dds = make_dds(dxt5);
        const auto parsed = dds_bc::parse(as_bytes(dds));
        assert(parsed.ok());
        const auto preview = dds_bc::decode_base_mip_rgba8(
            as_bytes(dds), parsed.document);
        assert(preview.ok);
        require_red_preview({
            preview.image.width,
            preview.image.height,
            preview.image.rgba8});

        const auto set = texture_set::parse_dds(as_bytes(dds));
        assert(set.kind == texture_set::Kind::standalone_dds);
        assert(set.slots.size() == 1U);
        require_texture_set_red(set, as_bytes(dds));

        const auto result = run_decode_pipeline(
            dxt5 ? "sample_dxt5.dds" : "sample_dxt1.dds",
            dds.data(), dds.size());
        assert(result.accepted);
        assert(!result.renderable);
        assert(has_capability(result.capabilities,
                              ResourceCapability::ImagePreview));
        assert(has_module(result, "native.texture-set"));
        require_red_preview(result.image_preview);

        const auto capped = dds_bc::decode_base_mip_rgba8(
            as_bytes(dds), parsed.document, 15U);
        assert(!capped.ok);
        assert(!capped.image.available());
    }

    const auto partial_mips = make_dds(false, 1U);
    const auto partial_parse = dds_bc::parse(as_bytes(partial_mips));
    assert(partial_parse.ok());
    const auto partial_set = texture_set::parse_dds(as_bytes(partial_mips));
    assert(partial_set.kind == texture_set::Kind::standalone_dds);
    require_texture_set_red(partial_set, as_bytes(partial_mips));
    const auto partial_result = run_decode_pipeline(
        "standalone_partial.dds", partial_mips.data(), partial_mips.size());
    assert(partial_result.accepted);
    require_red_preview(partial_result.image_preview);

    const auto wrapped = make_wrapped_dds(false);
    const auto wrapped_set = texture_set::parse_dds(as_bytes(wrapped));
    assert(wrapped_set.kind == texture_set::Kind::wrapped_dds);
    assert(wrapped_set.slots.size() == 1U);
    require_texture_set_red(wrapped_set, as_bytes(wrapped));
    const auto wrapped_result = run_decode_pipeline(
        "wrapped.dds", wrapped.data(), wrapped.size());
    assert(wrapped_result.accepted);
    assert(has_module(wrapped_result, "native.texture-set"));
    require_red_preview(wrapped_result.image_preview);
    assert(wrapped_result.inspection.root.children.size() == 1U);

    std::vector<std::uint8_t> huge(128U, 0U);
    huge[0] = 'D'; huge[1] = 'D'; huge[2] = 'S'; huge[3] = ' ';
    put_u32(huge, 4U, 124U);
    put_u32(huge, 12U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 16U, std::numeric_limits<std::uint32_t>::max());
    put_u32(huge, 28U, 32U);
    put_u32(huge, 76U, 32U);
    put_u32(huge, 80U, 4U);
    huge[84] = 'D'; huge[85] = 'X'; huge[86] = 'T'; huge[87] = '5';
    const auto huge_result = dds_bc::parse(as_bytes(huge));
    assert(!huge_result.ok());
    assert(!texture_set::parse_dds(as_bytes(huge)).ok());

    const auto ptx = make_ptx_zero_final_span();
    const auto ptx_set = texture_set::parse_ptx(as_bytes(ptx));
    assert(ptx_set.kind == texture_set::Kind::ptx_bundle);
    assert(ptx_set.slots.size() == 1U);
    require_texture_set_red(ptx_set, as_bytes(ptx));
    const auto ptx_ok = run_decode_pipeline("sample.ptx", ptx.data(), ptx.size());
    assert(ptx_ok.accepted);
    assert(has_module(ptx_ok, "native.texture-set"));
    assert(has_capability(ptx_ok.capabilities,
                          ResourceCapability::ChildResources));
    assert(!has_capability(ptx_ok.capabilities,
                           ResourceCapability::ImagePreview));
    assert(ptx_ok.inspection.root.children.size() == 1U);
    assert(ptx_ok.inspection.root.children[0].children.size() == 1U);

    assert(ptx_ok.children.size() == 1U);
    const auto& child = ptx_ok.children[0];
    assert(child.title == "DDS 0");
    assert(child.suggested_filename == "texture_0.dds");
    assert(child.probe.format == dmcresource::Format::Dds);
    assert(child.probe.content_confirmed);
    assert(has_capability(child.capabilities, ResourceCapability::Inspection));
    assert(has_capability(child.capabilities, ResourceCapability::ImagePreview));
    assert(child.inspection.format == "DDS");
    assert(child.inspection.root.title == "Texture 0");
    require_red_preview(child.image_preview);

    constexpr std::uint32_t retail_auxiliary_value = 0x1D308000U;
    const auto em000_like = make_ptx_zero_final_span(
        false, 2U, retail_auxiliary_value);
    const auto em000_set = texture_set::parse_ptx(as_bytes(em000_like));
    assert(em000_set.ok());
    assert(em000_set.kind == texture_set::Kind::ptx_bundle);
    assert(em000_set.ptx_aux_compat_used);
    require_texture_set_red(em000_set, as_bytes(em000_like));
    const auto em000_like_result = run_decode_pipeline(
        "em000_000.ptx", em000_like.data(), em000_like.size());
    assert(em000_like_result.accepted);
    assert(em000_like_result.children.size() == 1U);
    assert(has_module(em000_like_result, "native.ptx-aux-compat"));
    require_red_preview(em000_like_result.children[0].image_preview);

    auto unpaired_aux = em000_like;
    put_u32(unpaired_aux, 0x800U + 0x40U, 0U);
    assert(!texture_set::parse_ptx(as_bytes(unpaired_aux)).ok());
    assert(!run_decode_pipeline(
        "unpaired_aux.ptx", unpaired_aux.data(), unpaired_aux.size()).accepted);

    auto bad_descriptor_size = ptx;
    put_u32(bad_descriptor_size, 0x800U + 0x64U, 1U);
    assert(!run_decode_pipeline("bad.ptx", bad_descriptor_size.data(),
                                bad_descriptor_size.size()).accepted);

    auto trailing = ptx;
    trailing.push_back(0U);
    assert(!run_decode_pipeline("trailing.ptx", trailing.data(),
                                trailing.size()).accepted);

    auto sector_oob = make_ptx_sector_bounded();
    put_u32(sector_oob, 4U, 2U);
    assert(!run_decode_pipeline("sector_oob.ptx", sector_oob.data(),
                                sector_oob.size()).accepted);

    auto nonzero_padding = make_ptx_sector_bounded();
    nonzero_padding.back() = 1U;
    assert(!run_decode_pipeline("padding.ptx", nonzero_padding.data(),
                                nonzero_padding.size()).accepted);

    // Community-tool descriptors: format word, secondary dimensions and
    // reciprocal floats zeroed or copied. The strict reader rejects them as a
    // descriptor mismatch; the viewer reads header, sector span and DDS only.
    {
        auto community = make_ptx_sector_bounded();
        for (const std::size_t field : {0x08U, 0x0CU, 0x38U, 0x3CU, 0x40U, 0x44U}) {
            put_u32(community, 0x800U + field, 0U);
        }
        put_u32(community, 0x800U + 0x48U, 0x3B800000U);
        put_u32(community, 0x800U + 0x4CU, 0x3C000000U);
        const auto strict = dmc::rengine::profiles::dmc3::TextureSlotFramingReader::parse(
            as_bytes(community));
        assert(!strict.ok());
        bool aux = true;
        bool lenient = false;
        const auto read = dmcresource::ptx_compat::parse_texture_bundle(
            as_bytes(community), &aux, &lenient);
        assert(read.ok() && lenient && !aux);
        assert(read.document.textures.size() == 1U);
        const auto session = run_decode_pipeline("community.ptx", community.data(),
                                                 community.size());
        assert(session.accepted);
        assert(session.detail.find("community tool") != std::string::npos);

        // Structural faults stay rejected even with community descriptors.
        auto broken = community;
        broken.back() = 1U;
        assert(!run_decode_pipeline("community_padding.ptx", broken.data(),
                                    broken.size()).accepted);
    }

    // Whole PTX -> required nonzero slots -> renderer, without gallery previews.
    // Four physical slots have independent DDS colours; slot 0 is unused.
    std::vector<std::uint8_t> bundle(5U * 0x800U, 0U);
    put_u32(bundle, 0U, 4U);
    const std::uint16_t colours[] = {0xF800U, 0x07E0U, 0xF800U, 0x001FU};
    for (std::size_t i = 0U; i < 4U; ++i) {
        put_u32(bundle, 4U + i * 4U, 1U);
        auto child = make_ptx_zero_final_span(false, 2U, 1U);
        put_u16(child, 0x870U + 128U, colours[i]);
        put_u16(child, 0x870U + 130U, 0U);
        std::copy(child.begin() + 0x800U, child.end(),
                  bundle.begin() + (i + 1U) * 0x800U);
    }
    const auto original_bundle = bundle;
    dmcresource::Mesh mesh;
    mesh.vertices = {{-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.0F},
                     {1.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}};
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.uv0 = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    std::vector<std::uint32_t> slots{1U, 3U};
    const dmcresource::texture_companion::ModelTextureView model{&mesh, nullptr, slots};
    const auto attachment = dmcresource::texture_companion::attach_ptx(
        "four-slots.ptx", bundle.data(), bundle.size(), model);
    assert(attachment.attached);
    assert(attachment.required_slot_count == 2U);
    assert(attachment.source_texture_count == 4U);
    assert(attachment.textures.size() == 4U);
    assert(!attachment.textures[0].available());
    assert(!attachment.textures[2].available());
    assert(attachment.textures[1].rgba8[1] == 255U);
    assert(attachment.textures[3].rgba8[2] == 255U);
    assert(bundle == original_bundle); // compatibility must not mutate source

    dmcresource::ViewState view;
    view.yaw_radians = 0.0F;
    view.pitch_radians = 0.0F;
    const auto rendered = dmcresource::render_view(
        mesh, 128, 128, view, nullptr, &slots, &attachment.textures);
    bool green = false, blue = false;
    for (std::size_t i = 0U; i < rendered.pixels.size(); i += 4U) {
        green |= rendered.pixels[i] == 0U && rendered.pixels[i + 1U] == 255U &&
                 rendered.pixels[i + 2U] == 0U;
        blue |= rendered.pixels[i] == 0U && rendered.pixels[i + 1U] == 0U &&
                rendered.pixels[i + 2U] == 255U;
    }
    assert(green && blue);

    // Portable session owns attachment publication and survives child navigation.
    dmcresource::Session session;
    session.renderable = true;
    session.capabilities = dmcresource::capability(ResourceCapability::Geometry) |
        ResourceCapability::TextureBinding | ResourceCapability::UvCoordinates;
    session.render_mesh = mesh;
    session.render_triangle_texture_slots = slots;
    assert(dmcresource::spider::actions::attach_ptx(&session, "bundle.ptx", bundle.data(), bundle.size()));
    namespace widow = dmcresource::spider::black_widow;
    assert(widow::has_state(dmcresource::black_widow_state(&session),
                           widow::StateFlag::TextureCompanionAttached));
    assert(dmcresource::render_session(&session, 128, 128, 0.0F, 0.0F, view.zoom, 0U).pixels
           == rendered.pixels);
    {
        auto uv_gallery = dmcresource::open_uv_gallery(&session);
        assert(uv_gallery && dmcresource::session_child_count(uv_gallery.get()) == 2);
        assert(uv_gallery->uv_gallery->maps[0].texture_slot == 1U);
        assert(uv_gallery->uv_gallery->maps[1].texture_slot == 3U);
        auto uv_map = dmcresource::open_session_child(uv_gallery.get(), 1);
        assert(uv_map && !dmcresource::render_session(
            uv_map.get(),128,128,0,0,1,0).pixels.empty());
    }
    assert(dmcresource::render_session(&session,128,128,0,0,view.zoom,0).pixels
           == rendered.pixels);
    assert(!dmcresource::spider::actions::attach_ptx(&session, "bad.ptx", nullptr, 0U));
    assert(session.texture_companion_attached);
    assert(session.attached_textures[3].rgba8 == attachment.textures[3].rgba8);

    auto gallery = dmcresource::open_session("bundle.ptx", bundle.data(), bundle.size());
    assert(gallery && gallery->children.size() == 4U);
    auto child_session = dmcresource::session_from_child(gallery->children[1]);
    gallery.reset();
    assert(child_session->image_preview.available());
    assert(child_session->image_preview.rgba8[1] == 255U);

    slots[1] = 4U;
    const auto missing = dmcresource::texture_companion::attach_ptx(
        "missing.ptx", bundle.data(), bundle.size(), model);
    assert(!missing.attached && missing.textures.empty());
    slots[1] = std::numeric_limits<std::uint32_t>::max();
    assert(!dmcresource::texture_companion::can_attach(model));
    assert(!dmcresource::texture_companion::attach_ptx(
        "incomplete.ptx", bundle.data(), bundle.size(), model).attached);

    return 0;
}
