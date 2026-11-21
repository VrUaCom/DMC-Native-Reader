// Portable CLI companion for DMC Native Reader.
// Provides inspection topics, report generation, workspace scan, PNG export, and benchmarking.
// Standard C++23, zero Windows-specific dependencies.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "capi/include/dmc_reader.h"
#include "dmcresource/dmc_resource.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"
#include "dmcresource/spider/stage_actions.h"

#ifndef DMC_NATIVE_READER_BUILD_SHA
#define DMC_NATIVE_READER_BUILD_SHA "unknown"
#endif

namespace fs = std::filesystem;
using dmcresource::InspectionKind;
using dmcresource::InspectionNode;
using dmcresource::Session;
using dmcresource::spider::black_widow::has_state;
using dmcresource::spider::black_widow::StateFlag;

namespace {

std::vector<std::uint8_t> ReadFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string JsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

constexpr const char* InspectionKindName(InspectionKind kind) {
    switch (kind) {
        case InspectionKind::Document: return "Document";
        case InspectionKind::Header: return "Header";
        case InspectionKind::Collection: return "Collection";
        case InspectionKind::Object: return "Object";
        case InspectionKind::Mesh: return "Mesh";
        case InspectionKind::Node: return "Node";
        case InspectionKind::Bone: return "Bone";
        case InspectionKind::Transform: return "Transform";
        case InspectionKind::Skin: return "Skin";
        case InspectionKind::Texture: return "Texture";
        case InspectionKind::MaterialState: return "MaterialState";
        case InspectionKind::Container: return "Container";
        case InspectionKind::Text: return "Text";
        case InspectionKind::Diagnostic: return "Diagnostic";
        default: return "Unknown";
    }
}

void WriteInspectionNodeJson(std::ostream& out, const InspectionNode& node) {
    out << "{\"id\":\"" << JsonEscape(node.id) << "\",\"title\":\"" << JsonEscape(node.title)
        << "\",\"kind\":\"" << InspectionKindName(node.kind) << "\"";
    if (node.source_span.has_value()) {
        out << ",\"offset\":" << node.source_span->offset << ",\"size\":" << node.source_span->size;
    }
    out << ",\"properties\":[";
    for (std::size_t i = 0; i < node.properties.size(); ++i) {
        if (i != 0) out << ",";
        const auto& p = node.properties[i];
        out << "{\"key\":\"" << JsonEscape(p.key) << "\",\"value\":\"" << JsonEscape(p.value)
            << "\"}";
    }
    out << "],\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i != 0) out << ",";
        WriteInspectionNodeJson(out, node.children[i]);
    }
    out << "]}";
}

struct NamedFlag {
    StateFlag flag;
    const char* name;
};
constexpr NamedFlag kAllFlags[] = {
    {StateFlag::CanRender, "CanRender"},
    {StateFlag::CanWireframe, "CanWireframe"},
    {StateFlag::CanInspect, "CanInspect"},
    {StateFlag::CanShowHierarchy, "CanShowHierarchy"},
    {StateFlag::HasSkinning, "HasSkinning"},
    {StateFlag::HasSkinWeights, "HasSkinWeights"},
    {StateFlag::HasTextureBindings, "HasTextureBindings"},
    {StateFlag::CanPreviewImage, "CanPreviewImage"},
    {StateFlag::HasChildResources, "HasChildResources"},
    {StateFlag::IsText, "IsText"},
    {StateFlag::IsContainer, "IsContainer"},
    {StateFlag::HasCollision, "HasCollision"},
    {StateFlag::HasAdjacency, "HasAdjacency"},
    {StateFlag::HasTransformSelectors, "HasTransformSelectors"},
    {StateFlag::CanShowUv, "CanShowUv"},
    {StateFlag::ChildBrowserMode, "ChildBrowserMode"},
    {StateFlag::TextureCompanionAttachable, "TextureCompanionAttachable"},
    {StateFlag::TextureCompanionAttached, "TextureCompanionAttached"},
    {StateFlag::UvMapView, "UvMapView"},
    {StateFlag::CanInspectUv, "CanInspectUv"},
    {StateFlag::CanInspectMeshes, "CanInspectMeshes"},
    {StateFlag::CanInspectHierarchy, "CanInspectHierarchy"},
    {StateFlag::CanExportPng, "CanExportPng"},
    {StateFlag::CanAddModelPart, "CanAddModelPart"},
    {StateFlag::CanStageCompanion, "CanStageCompanion"},
};

std::vector<std::string> ActiveCapabilityNames(dmcresource::spider::black_widow::StateBits bits) {
    std::vector<std::string> out;
    for (const auto& f : kAllFlags) {
        if (has_state(bits, f.flag)) out.emplace_back(f.name);
    }
    return out;
}

std::unique_ptr<Session> OpenPath(const fs::path& path, std::string* error) {
    const auto bytes = ReadFileBytes(path);
    if (bytes.empty()) {
        *error = "could not read file or file is empty";
        return nullptr;
    }
    try {
        auto session = dmcresource::open_session(path.string(), bytes.data(), bytes.size());
        if (!session) *error = "open_session returned null";
        return session;
    } catch (const std::exception& e) {
        *error = e.what();
        return nullptr;
    } catch (...) {
        *error = "unknown exception opening resource";
        return nullptr;
    }
}

// ---- PNG Encoding ---------------------------------------------------------

uint32_t Crc32(const uint8_t* buf, std::size_t len) {
    static const auto table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        c = table[(c ^ buf[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

uint32_t Adler32(const uint8_t* buf, std::size_t len) {
    uint32_t s1 = 1U;
    uint32_t s2 = 0U;
    for (std::size_t i = 0; i < len; ++i) {
        s1 = (s1 + buf[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

void WritePngChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, std::size_t len) {
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

bool SaveRgbaToPng(const fs::path& path, int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0 || rgba == nullptr) return false;
    std::vector<uint8_t> png;
    // PNG Header
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);

    // IHDR
    uint8_t ihdr[13];
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(width & 0xFF);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(height & 0xFF);
    ihdr[8] = 8;   // 8 bits per channel
    ihdr[9] = 6;   // RGBA
    ihdr[10] = 0;  // Compression method (deflate)
    ihdr[11] = 0;  // Filter method
    ihdr[12] = 0;  // Interlace (none)
    WritePngChunk(png, "IHDR", ihdr, 13);

    // IDAT with uncompressed deflate blocks
    std::vector<uint8_t> raw_stream;
    raw_stream.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4));
    for (int y = 0; y < height; ++y) {
        raw_stream.push_back(0);  // Filter: None
        const uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        raw_stream.insert(raw_stream.end(), row, row + static_cast<std::size_t>(width) * 4);
    }

    std::vector<uint8_t> idat_data;
    idat_data.push_back(0x78);  // Zlib header
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

// ---- Commands -------------------------------------------------------------

int CmdInspect(const fs::path& path, std::string topic_str) {
    const auto bytes = ReadFileBytes(path);
    if (bytes.empty()) {
        std::cerr << "error: could not read file: " << path.string() << "\n";
        return 1;
    }

    auto res = dmc_open(path.string().c_str(), bytes.data(), bytes.size());
    if (res.session == nullptr) {
        std::cerr << "error: rejected: " << (res.title ? res.title : "unknown reason") << "\n";
        dmc_string_free(res.title);
        return 1;
    }
    dmc_string_free(res.title);

    int topic = 0;
    if (!topic_str.empty()) {
        std::transform(topic_str.begin(), topic_str.end(), topic_str.begin(), ::tolower);
        if (topic_str == "1" || topic_str == "uv") topic = 1;
        else if (topic_str == "2" || topic_str == "mesh" || topic_str == "meshes") topic = 2;
        else if (topic_str == "3" || topic_str == "hierarchy" || topic_str == "bones") topic = 3;
    }

    if (topic >= 1 && topic <= 3) {
        char* text = dmc_inspect(res.session, topic);
        if (text) {
            std::cout << text << "\n";
            dmc_string_free(text);
        }
        dmc_close(res.session);
        return 0;
    }

    std::string error;
    auto session = OpenPath(path, &error);
    if (!session) {
        std::cerr << "error: " << path.string() << ": " << error << "\n";
        dmc_close(res.session);
        return 1;
    }

    const auto& probe = session->probe;
    std::cout << "build_sha: " << DMC_NATIVE_READER_BUILD_SHA << "\n";
    std::cout << "path: " << path.string() << "\n";
    std::cout << "family: " << probe.family << "\n";
    std::cout << "domain: " << probe.domain << "\n";
    std::cout << "support: " << probe.support << "\n";
    std::cout << "evidence: " << probe.evidence << "\n";
    std::cout << "identity: "
              << (probe.content_confirmed ? "content-confirmed" : "extension/name-only") << "\n";
    std::cout << "renderable: " << (session->renderable ? "true" : "false") << "\n";
    std::cout << "composite_parts: " << session->composite_parts.size() << "\n";
    std::cout << "children: " << dmcresource::session_child_count(session.get()) << "\n";

    char* report = nullptr;
    const int health = dmc_file_health(res.session, path.filename().string().c_str(), &report);
    std::cout << "health: " << (health == 0 ? "clean" : (health == 1 ? "warning" : "problem")) << "\n";
    if (report && *report) {
        std::cout << "health_report: " << report << "\n";
        dmc_string_free(report);
    }

    const auto bits = dmcresource::black_widow_state(session.get());
    std::cout << "capabilities:";
    for (const auto& name : ActiveCapabilityNames(bits)) std::cout << " " << name;
    std::cout << "\n";

    if (!session->detail.empty()) std::cout << "detail: " << session->detail << "\n";
    if (!session->trace.empty()) std::cout << "trace: " << session->trace << "\n";

    if (!session->inspection.empty()) {
        std::cout << "\n--- inspection tree ---\n"
                  << dmcresource::format_inspection_tree(session->inspection);
    }

    dmc_close(res.session);
    return 0;
}

int CmdReport(const fs::path& path, const fs::path& out_path) {
    std::string error;
    auto session = OpenPath(path, &error);
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "error: could not write " << out_path.string() << "\n";
        return 1;
    }
    if (!session) {
        out << "{\"path\":\"" << JsonEscape(path.string()) << "\",\"ok\":false,\"error\":\""
            << JsonEscape(error) << "\"}";
        std::cerr << "error: " << path.string() << ": " << error << "\n";
        return 1;
    }

    const auto& probe = session->probe;
    const auto bits = dmcresource::black_widow_state(session.get());
    out << "{";
    out << "\"build_sha\":\"" << DMC_NATIVE_READER_BUILD_SHA << "\",";
    out << "\"path\":\"" << JsonEscape(path.string()) << "\",";
    out << "\"ok\":true,";
    out << "\"family\":\"" << JsonEscape(probe.family) << "\",";
    out << "\"domain\":\"" << JsonEscape(probe.domain) << "\",";
    out << "\"support\":\"" << JsonEscape(probe.support) << "\",";
    out << "\"evidence\":\"" << JsonEscape(probe.evidence) << "\",";
    out << "\"content_confirmed\":" << (probe.content_confirmed ? "true" : "false") << ",";
    out << "\"renderable\":" << (session->renderable ? "true" : "false") << ",";
    out << "\"composite_parts\":" << session->composite_parts.size() << ",";
    out << "\"children\":" << dmcresource::session_child_count(session.get()) << ",";
    out << "\"detail\":\"" << JsonEscape(session->detail) << "\",";
    out << "\"trace\":\"" << JsonEscape(session->trace) << "\",";
    out << "\"capabilities\":[";
    const auto names = ActiveCapabilityNames(bits);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) out << ",";
        out << "\"" << names[i] << "\"";
    }
    out << "],";
    out << "\"inspection\":";
    if (session->inspection.empty()) {
        out << "null";
    } else {
        WriteInspectionNodeJson(out, session->inspection.root);
    }
    out << "}";
    std::cout << "wrote " << out_path.string() << "\n";
    return 0;
}

int CmdWorkspaceScan(const fs::path& folder, const fs::path& out_path) {
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::cerr << "error: not a directory: " << folder.string() << "\n";
        return 1;
    }
    constexpr std::uintmax_t kMaxScanBytes = 256ULL * 1024 * 1024;

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::cerr << "error: could not write " << out_path.string() << "\n";
        return 1;
    }

    out << "[";
    bool first = true;
    std::size_t scanned = 0, recognized = 0, content_confirmed = 0, skipped_large = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto& entry_path = it->path();
        const auto file_size = it->file_size(ec);
        if (ec) continue;
        ++scanned;
        if (!first) out << ",";
        first = false;

        if (file_size > kMaxScanBytes) {
            ++skipped_large;
            out << "{\"path\":\"" << JsonEscape(entry_path.string()) << "\",\"size\":" << file_size
                << ",\"skipped\":\"too_large\"}";
            continue;
        }

        const auto bytes = ReadFileBytes(entry_path);
        const auto probe_result =
            dmcresource::probe(entry_path.string(), bytes.data(), bytes.size());
        if (probe_result.recognized) ++recognized;
        if (probe_result.content_confirmed) ++content_confirmed;

        out << "{\"path\":\"" << JsonEscape(entry_path.string()) << "\",\"size\":" << file_size
            << ",\"family\":\"" << JsonEscape(probe_result.family) << "\",\"recognized\":"
            << (probe_result.recognized ? "true" : "false")
            << ",\"content_confirmed\":" << (probe_result.content_confirmed ? "true" : "false")
            << "}";
    }
    out << "]";
    std::cout << "scanned=" << scanned << " recognized=" << recognized
              << " content_confirmed=" << content_confirmed << " skipped_large=" << skipped_large
              << "\nwrote " << out_path.string() << "\n";
    return 0;
}

int CmdExport(const fs::path& input_path, const fs::path& out_path) {
    const auto bytes = ReadFileBytes(input_path);
    if (bytes.empty()) {
        std::cerr << "error: could not read " << input_path.string() << "\n";
        return 1;
    }

    auto res = dmc_open(input_path.string().c_str(), bytes.data(), bytes.size());
    if (res.session == nullptr) {
        std::cerr << "error: open rejected: " << (res.title ? res.title : "unknown reason") << "\n";
        dmc_string_free(res.title);
        return 1;
    }
    dmc_string_free(res.title);

    int img_w = 0, img_h = 0;
    std::vector<uint8_t> pixels;

    dmc_image* export_img = dmc_render_export(res.session);
    if (export_img != nullptr && export_img->rgba != nullptr) {
        img_w = export_img->width;
        img_h = export_img->height;
        pixels.assign(export_img->rgba, export_img->rgba + static_cast<std::size_t>(img_w * img_h * 4));
        dmc_image_free(export_img);
    } else if (dmc_image_preview_size(res.session, &img_w, &img_h) && img_w > 0 && img_h > 0) {
        pixels.resize(static_cast<std::size_t>(img_w * img_h * 4));
        if (!dmc_image_preview(res.session, pixels.data(), pixels.size())) {
            std::cerr << "error: failed to read image preview\n";
            dmc_close(res.session);
            return 1;
        }
    } else {
        img_w = 1024;
        img_h = 1024;
        dmc_view view{};
        dmc_view_reset(&view);
        pixels.resize(static_cast<std::size_t>(img_w * img_h * 4));
        const int rc = dmc_render(res.session, img_w, img_h, &view, pixels.data(), pixels.size());
        if (rc == 0) {
            std::cerr << "error: render failed for " << input_path.string() << "\n";
            dmc_close(res.session);
            return 1;
        }
    }

    if (!SaveRgbaToPng(out_path, img_w, img_h, pixels.data())) {
        std::cerr << "error: failed to write PNG to " << out_path.string() << "\n";
        dmc_close(res.session);
        return 1;
    }

    std::cout << "exported " << img_w << "x" << img_h << " PNG to " << out_path.string() << "\n";
    dmc_close(res.session);
    return 0;
}

int CmdBenchmark(const fs::path& input_path, float seconds) {
    const auto bytes = ReadFileBytes(input_path);
    if (bytes.empty()) {
        std::cerr << "error: could not read " << input_path.string() << "\n";
        return 1;
    }

    auto res = dmc_open(input_path.string().c_str(), bytes.data(), bytes.size());
    if (res.session == nullptr) {
        std::cerr << "error: open rejected: " << (res.title ? res.title : "unknown reason") << "\n";
        dmc_string_free(res.title);
        return 1;
    }
    dmc_string_free(res.title);

    dmc_view view{};
    dmc_view_reset(&view);

    std::cout << "running benchmark for " << seconds << " seconds on " << input_path.string() << "...\n";
    char* report = dmc_benchmark(res.session, 1024, 768, &view, 0.0f, seconds, "Default Settings");
    if (report) {
        std::cout << report << "\n";
        dmc_string_free(report);
    } else {
        std::cout << "benchmark returned no report\n";
    }

    dmc_close(res.session);
    return 0;
}

void PrintUsage() {
    std::cerr << "dmc_native_reader_cli <command> [args]\n"
              << "  inspect <file> [topic: uv | mesh | hierarchy | all]\n"
              << "  report <file> --json <out.json>\n"
              << "  workspace-scan <folder> --json <out.json>\n"
              << "  export <file> <out.png>\n"
              << "  benchmark <file> [seconds]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::string_view command = argv[1];
    if (command == "inspect" && argc >= 3) {
        const std::string topic = (argc >= 4) ? argv[3] : "";
        return CmdInspect(fs::path(argv[2]), topic);
    }
    if (command == "report" && argc >= 5 && std::string_view(argv[3]) == "--json") {
        return CmdReport(fs::path(argv[2]), fs::path(argv[4]));
    }
    if (command == "workspace-scan" && argc >= 5 && std::string_view(argv[3]) == "--json") {
        return CmdWorkspaceScan(fs::path(argv[2]), fs::path(argv[4]));
    }
    if (command == "export" && argc >= 4) {
        return CmdExport(fs::path(argv[2]), fs::path(argv[3]));
    }
    if (command == "benchmark" && argc >= 3) {
        float secs = 5.0f;
        if (argc >= 4) {
            try { secs = std::stof(argv[3]); } catch (...) {}
        }
        return CmdBenchmark(fs::path(argv[2]), secs);
    }
    PrintUsage();
    return 2;
}
