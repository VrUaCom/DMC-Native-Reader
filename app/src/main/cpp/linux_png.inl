#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace linux_png {

inline uint32_t Crc32(const uint8_t* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
        }
    }
    return ~crc;
}

inline uint32_t Adler32(const uint8_t* data, std::size_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (std::size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

inline void WritePngChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, std::size_t len) {
    out.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(len & 0xFF));
    const std::size_t type_start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len > 0 && data != nullptr) {
        out.insert(out.end(), data, data + len);
    }
    const uint32_t crc = Crc32(out.data() + type_start, 4 + len);
    out.push_back(static_cast<uint8_t>((crc >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(crc & 0xFF));
}

inline bool SaveRgbaToPng(const std::filesystem::path& path, int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0 || rgba == nullptr) return false;
    std::vector<uint8_t> png;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(width & 0xFF);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(height & 0xFF);
    ihdr[8] = 8;
    ihdr[9] = 6;   // RGBA
    ihdr[10] = 0;  // Deflate
    ihdr[11] = 0;  // Filter
    ihdr[12] = 0;  // Non-interlaced
    WritePngChunk(png, "IHDR", ihdr, 13);

    std::vector<uint8_t> raw_stream;
    raw_stream.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4));
    for (int y = 0; y < height; ++y) {
        raw_stream.push_back(0); // Filter: none
        const uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        raw_stream.insert(raw_stream.end(), row, row + static_cast<std::size_t>(width) * 4);
    }

    std::vector<uint8_t> idat_data;
    idat_data.push_back(0x78);
    idat_data.push_back(0x01);

    const std::size_t kMaxBlock = 32768;
    std::size_t offset = 0;
    while (offset < raw_stream.size()) {
        const std::size_t block_len = std::min(kMaxBlock, raw_stream.size() - offset);
        const bool is_final = (offset + block_len == raw_stream.size());
        idat_data.push_back(is_final ? 0x01 : 0x00);
        const uint16_t len = static_cast<uint16_t>(block_len);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        idat_data.push_back(static_cast<uint8_t>(len & 0xFF));
        idat_data.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        idat_data.push_back(static_cast<uint8_t>(nlen & 0xFF));
        idat_data.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        idat_data.insert(idat_data.end(), raw_stream.data() + offset, raw_stream.data() + offset + block_len);
        offset += block_len;
    }

    const uint32_t adler = Adler32(raw_stream.data(), raw_stream.size());
    idat_data.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    idat_data.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    idat_data.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    idat_data.push_back(static_cast<uint8_t>(adler & 0xFF));

    WritePngChunk(png, "IDAT", idat_data.data(), idat_data.size());
    WritePngChunk(png, "IEND", nullptr, 0);

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

} // namespace linux_png
