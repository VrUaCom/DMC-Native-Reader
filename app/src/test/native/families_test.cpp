#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dmcresource/decode_pipeline.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/native_module.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/sound.h"

using namespace dmcresource;

namespace {

struct Writer {
    std::vector<std::uint8_t> bytes;
    void text(std::string_view t) { bytes.insert(bytes.end(), t.begin(), t.end()); }
    void u32(std::uint32_t v) { put(&v, 4); }
    void be32(std::uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) bytes.push_back(static_cast<std::uint8_t>(v >> s));
    }
    void f32(float v) { put(&v, 4); }
    void zeros(std::size_t n) { bytes.insert(bytes.end(), n, 0U); }
    void put(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
};

std::string expect(const std::vector<std::uint8_t>& bytes, Format format, std::string_view family,
                   std::string_view in_info) {
    const auto probe = dmcresource::probe("renamed.bin", bytes.data(), bytes.size());
    assert(probe.format == format);
    assert(std::string_view{format_name(format)} == family);
    const auto* module = NativeModuleRegistry::find(family);
    assert(module != nullptr && module->format == format && !module->renderable);
    const auto result = run_decode_pipeline("renamed.bin", bytes.data(), bytes.size());
    assert(result.accepted);
    const auto info = format_inspection_tree(result.inspection);
    assert(info.find(in_info) != std::string::npos);
    return info;
}

}  // namespace

int main() {
    // LIG2: header 0x20, 48 records of 0x30.
    Writer lig;
    lig.text("LIG2");
    lig.zeros(0x1C);
    for (int r = 0; r < 48; ++r) {
        for (int w = 0; w < 12; ++w) lig.f32(0.25F * static_cast<float>(w));
    }
    expect(lig.bytes, Format::Lig2, "LIG2", "48 of 0x30");

    // DCA: header 0x10, records of 0x410.
    Writer dca;
    dca.text(std::string_view{"DCA\0", 4});
    dca.zeros(12);
    dca.zeros(2 * 0x410);
    expect(dca.bytes, Format::Dca, "DCA", "2 of 0x410");

    // Stage text with directives; a plain sentence is not stage text.
    Writer txt;
    txt.text("// room\r\n#SET 1 BREAK\r\nDOOR 0 1 2\r\nNEXTROOM 101\r\n");
    expect(txt.bytes, Format::StageText, "TXT", "DOOR: 1");
    const std::string_view plain = "hello world\n";
    assert(probe("note.bin", reinterpret_cast<const std::uint8_t*>(plain.data()), plain.size()).format !=
           Format::StageText);

    // .index name list, with its PNST marker line.
    Writer index;
    index.text("PNST\r\nst100_000.scm\r\nst100_001.ptx\r\n");
    index.zeros(4);
    expect(index.bytes, Format::NameIndex, ".index", "slot 1: st100_001.ptx");

    // The SO "volume table" of dmc-rengine-cpp is the collision shape table
    // the Reader reads from the EXE: such bytes stay COLSHAPE.
    Writer volumes;
    for (int r = 0; r < 3; ++r) {
        const bool segment = r == 2;
        volumes.u32(segment ? 4U : 2U);
        volumes.zeros(12);
        volumes.f32(10.0F * r); volumes.f32(0.0F); volumes.f32(5.0F); volumes.f32(1.0F);
        if (segment) {
            volumes.f32(40.0F); volumes.f32(0.0F); volumes.f32(5.0F); volumes.f32(1.0F);
            volumes.f32(3.0F); volumes.zeros(12);
        } else {
            volumes.f32(4.0F); volumes.zeros(12);
            volumes.zeros(16);
        }
        volumes.zeros(16);
    }
    assert(probe("slot_0040.bin", volumes.bytes.data(), volumes.bytes.size()).format == Format::CollisionShapes);
    assert(NativeModuleRegistry::find("SO-VOLUMES") == nullptr && NativeModuleRegistry::find("SO-LINKS") == nullptr);
    assert(NativeModuleRegistry::find("SO-GRAPH") != nullptr);

    // dbsT: the word at +4 counts the 32-byte records.
    Writer tsb;
    tsb.text("dbsT");
    tsb.u32(2U); tsb.zeros(8);
    tsb.zeros(64);
    expect(tsb.bytes, Format::SoundTable, "dbsT", "+4 equals the record count");

    // ff&A: rows of four floats.
    Writer ff;
    ff.text("ff&A");
    ff.zeros(12);
    for (int r = 0; r < 56; ++r) { ff.f32(1.0F); ff.f32(2.0F); ff.f32(3.0F); ff.f32(4.0F); }
    expect(ff.bytes, Format::FloatTable, "ff&A", "56 of 16 bytes");

    // HID: the tag at +4.
    Writer hid;
    hid.u32(3U);
    hid.text(std::string_view{"HID\0", 4});
    hid.zeros(24);
    expect(hid.bytes, Format::Hid, "HID", "HID at +4");

    // SPUMAPDT.
    Writer spu;
    spu.text("SPUMAPDT");
    spu.zeros(24);
    expect(spu.bytes, Format::SpuMap, "SPUMAPDT", "fields open");

    // VAG: two chunks; the first decodes to a ramp and ends on its flag.
    Writer vag;
    const auto chunk = [&](std::string_view name, std::uint32_t rate) {
        vag.text("VAGp"); vag.be32(0x20U); vag.be32(0U); vag.be32(48U); vag.be32(rate);
        vag.zeros(12);
        std::string padded{name};
        padded.resize(16, '\0');
        vag.text(padded);
        vag.zeros(16);                                    // frame 0: silence
        vag.bytes.push_back(0x00U); vag.bytes.push_back(0U);  // frame 1: shift 0, filter 0
        for (int i = 0; i < 14; ++i) vag.bytes.push_back(0x77U);
        vag.bytes.push_back(0x00U); vag.bytes.push_back(1U);  // frame 2: end flag
        vag.zeros(14);
    };
    chunk("voice", 22050U);
    chunk("other", 11025U);
    {
        const std::span<const std::uint8_t> data{vag.bytes};
        const auto chunks = sound::read_vag(data);
        assert(chunks.size() == 2U && chunks[0].name == "voice" && chunks[1].sample_rate == 11025U);
        const auto samples = sound::decode_vag(data, chunks[0]);
        assert(samples.size() == 84U);
        assert(samples[0] == 0 && samples[28] == 7 * 4096);
    }
    expect(vag.bytes, Format::Vag, "VAG", "22050 Hz");
    {
        // The session keeps the sound and decodes it on demand for playback.
        auto session = open_session("voice.vag", vag.bytes.data(), vag.bytes.size());
        assert(session && session_sound_count(session.get()) == 2U);
        assert(session_sound_rate(session.get(), 1U) == 11025U);
        assert(session_sound_title(session.get(), 0U).find("voice") != std::string::npos);
        assert(session_sound_samples(session.get(), 0U).size() == 84U);
        assert(session_sound_samples(session.get(), 5U).empty());
        auto silent = open_session("lig.lig", lig.bytes.data(), lig.bytes.size());
        assert(silent && session_sound_count(silent.get()) == 0U);
    }
    {
        const auto result = run_decode_pipeline("voice.vag", vag.bytes.data(), vag.bytes.size());
        assert(result.image_preview.available());
    }
    return 0;
}
