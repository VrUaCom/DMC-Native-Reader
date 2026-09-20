// Headless CLI companion for DMC Native Reader (master plan section 5.1.G).
//
// Same role as win_shell.cpp: a thin platform host over DMCNativeReader::Core.
// No format parsing, no duplicated semantics -- every command delegates to the
// exact portable session/inspection API the GUI shell uses.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dmcresource/dmc_resource.h"
#include "dmcresource/inspection_format.h"
#include "dmcresource/resource_session.h"
#include "dmcresource/session_inspection.h"
#include "dmcresource/spider/black_widow.h"

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

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

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

std::string JsonEscape(const std::string& s) {
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
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
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

int CmdInspect(const fs::path& path) {
    std::string error;
    auto session = OpenPath(path, &error);
    if (!session) {
        std::wcerr << L"error: " << path.wstring() << L": " << Utf8ToWide(error) << L"\n";
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
    return 0;
}

int CmdReport(const fs::path& path, const fs::path& out_path) {
    std::string error;
    auto session = OpenPath(path, &error);
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::wcerr << L"error: could not write " << out_path.wstring() << L"\n";
        return 1;
    }
    if (!session) {
        out << "{\"path\":\"" << JsonEscape(path.string()) << "\",\"ok\":false,\"error\":\""
            << JsonEscape(error) << "\"}";
        std::wcerr << L"error: " << path.wstring() << L": " << Utf8ToWide(error) << L"\n";
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
    std::wcout << L"wrote " << out_path.wstring() << L"\n";
    return 0;
}

int CmdWorkspaceScan(const fs::path& folder, const fs::path& out_path) {
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        std::wcerr << L"error: not a directory: " << folder.wstring() << L"\n";
        return 1;
    }
    // Bounded: skip anything absurdly large rather than exhausting memory on
    // an unbounded scan (plan 5.1.B / section 11 stability goals).
    constexpr std::uintmax_t kMaxScanBytes = 256ULL * 1024 * 1024;

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::wcerr << L"error: could not write " << out_path.wstring() << L"\n";
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
    std::wcout << L"scanned=" << scanned << L" recognized=" << recognized
               << L" content_confirmed=" << content_confirmed << L" skipped_large=" << skipped_large
               << L"\nwrote " << out_path.wstring() << L"\n";
    return 0;
}

void PrintUsage() {
    std::wcerr << L"dmc-native-reader-cli.exe <command> [args]\n"
                  L"  inspect <file>\n"
                  L"  report <file> --json <out.json>\n"
                  L"  workspace-scan <folder> --json <out.json>\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::wstring command = argv[1];
    if (command == L"inspect" && argc >= 3) {
        return CmdInspect(fs::path(argv[2]));
    }
    if (command == L"report" && argc >= 5 && std::wstring(argv[3]) == L"--json") {
        return CmdReport(fs::path(argv[2]), fs::path(argv[4]));
    }
    if (command == L"workspace-scan" && argc >= 5 && std::wstring(argv[3]) == L"--json") {
        return CmdWorkspaceScan(fs::path(argv[2]), fs::path(argv[4]));
    }
    PrintUsage();
    return 2;
}
