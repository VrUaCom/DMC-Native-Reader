#include "dmcresource/decode_pipeline.h"

#include <sstream>
#include <string_view>

#include "dmcresource/native_module.h"

namespace dmcresource {
namespace {

[[nodiscard]] bool has_index_extension(std::string_view filename) noexcept {
    constexpr std::string_view extension = ".index";
    if (filename.size() < extension.size()) return false;
    const auto tail = filename.substr(filename.size() - extension.size());
    for (std::size_t i = 0; i < extension.size(); ++i) {
        char value = tail[i];
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value - 'A' + 'a');
        }
        if (value != extension[i]) return false;
    }
    return true;
}

}  // namespace

PipelineResult run_decode_pipeline(std::string_view filename,
                                   const std::uint8_t* bytes,
                                   std::size_t size) noexcept {
    PipelineResult rejected;

    // DMC3 .index files are textual extraction/naming metadata and may begin
    // with the literal line `PNST` or `PAC`. That four-byte text prefix must
    // not be promoted to binary-container authority. Force extension/name
    // identity for this metadata family, then let its own module validate text.
    rejected.probe = has_index_extension(filename)
        ? probe(filename, nullptr, 0u)
        : probe(filename, bytes, size);

    if (!rejected.probe.recognized) {
        rejected.detail = "pipeline rejected unknown resource";
        return rejected;
    }

    const auto* module = NativeModuleRegistry::find(
        rejected.probe.family != nullptr
            ? std::string_view{rejected.probe.family}
            : std::string_view{});
    if (module == nullptr || module->run == nullptr) {
        rejected.detail = "recognized resource has no registered native module";
        return rejected;
    }

    // The catalog owns recognition/evidence metadata; once a family has a
    // promoted module, the module registry owns its concrete Native Reader
    // format identity. This avoids keeping a second central Format switch in
    // the catalog while still allowing generic recognized families to remain
    // Format::Other.
    auto authoritative_probe = rejected.probe;
    if (module->format != Format::Unknown) {
        authoritative_probe.format = module->format;
    }
    return module->run(filename, bytes, size, authoritative_probe);
}

std::string pipeline_trace(const PipelineResult& result) {
    std::ostringstream out;
    out << "modules:";
    for (const auto& module : result.modules) {
        out << "\n  " << (module.complete ? "[OK] " : "[TODO] ") << module.name;
    }
    return out.str();
}

}  // namespace dmcresource
