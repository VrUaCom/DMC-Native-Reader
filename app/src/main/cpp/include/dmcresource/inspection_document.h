#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dmcresource {

enum class InspectionKind : std::uint8_t {
    Document,
    Header,
    Collection,
    Object,
    Mesh,
    Node,
    Bone,
    Transform,
    Skin,
    Texture,
    MaterialState,
    Container,
    Text,
    Diagnostic,
    Unknown,
};

enum class EvidenceLevel : std::uint8_t {
    Unknown,
    Recognized,
    StructuralConfirmed,
    DataConfirmed,
    ExeConfirmed,
    ExeAndCorpusConfirmed,
    PreservedUndecoded,
};

struct SourceSpan final {
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct InspectionProperty final {
    std::string key;
    std::string value;
    EvidenceLevel evidence{EvidenceLevel::Unknown};
};

struct InspectionNode final {
    std::string id;
    std::string title;
    InspectionKind kind{InspectionKind::Unknown};
    std::optional<SourceSpan> source_span;
    std::vector<InspectionProperty> properties;
    std::vector<InspectionNode> children;
};

struct InspectionDocument final {
    std::string format;
    InspectionNode root;

    [[nodiscard]] bool empty() const noexcept {
        return root.title.empty() && root.properties.empty() && root.children.empty();
    }
};

}  // namespace dmcresource
