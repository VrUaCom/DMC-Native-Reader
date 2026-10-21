#include "dmcresource/spider/app_info.h"

#include <array>
#include <utility>
#include <vector>

#include "dmcresource/native_module.h"

namespace dmcresource::spider::actions {
namespace {

enum class Group : std::uint8_t { Models, Textures, Archives, Motion, Stage, Events, Sound, Other };

[[nodiscard]] Group group_of(Format format) noexcept {
    switch (format) {
    case Format::Scm:
    case Format::Mod:
    case Format::Shw:
    case Format::Hits:
    case Format::CollisionShapes:
    case Format::AttackIndex: return Group::Models;
    case Format::Dds:
    case Format::Ptx: return Group::Textures;
    case Format::Pac:
    case Format::Pnst:
    case Format::EffectBank: return Group::Archives;
    case Format::Mot:
    case Format::MotionScript:
    case Format::Tsc:
    case Format::Clt: return Group::Motion;
    case Format::StagePos:
    case Format::StageEve:
    case Format::StageCam:
    case Format::StageItm:
    case Format::StageSte:
    case Format::StageEst:
    case Format::StageSef:
    case Format::Lig2:
    case Format::Dca:
    case Format::StageText:
    case Format::NameIndex:
    case Format::SoGraph: return Group::Stage;
    case Format::Evt: return Group::Events;
    case Format::Vag:
    case Format::SoundTable:
    case Format::SpuMap: return Group::Sound;
    default: return Group::Other;
    }
}

constexpr std::array<std::pair<Group, const char*>, 8> kGroups{{
    {Group::Models, "Models, scenes, collision"},
    {Group::Textures, "Textures"},
    {Group::Archives, "Archives"},
    {Group::Motion, "Motion and scripts"},
    {Group::Stage, "Stage"},
    {Group::Events, "Events"},
    {Group::Sound, "Sound"},
    {Group::Other, "Other"},
}};

}  // namespace

std::string welcome_text(std::string_view app_version, std::string_view native_build) {
    std::string text = "DMC Native Reader " + std::string{app_version} + "\nNative build: " +
                       std::string{native_build} + "\n\nReads (read-only, every file identified by its bytes):";
    for (const auto& [group, title] : kGroups) {
        std::string families;
        for (const auto& module : NativeModuleRegistry::modules()) {
            if (group_of(module.format) != group) continue;
            if (!families.empty()) families += ", ";
            families += module.family;
        }
        if (!families.empty()) text += "\n\xE2\x80\xA2 " + std::string{title} + ": " + families;
    }
    text += "\n\nTo start:"
            "\n\xE2\x80\xA2 Open a file from My Files or with \xE2\x86\x91: one file, a PAC, a whole stage st*.pac."
            "\n\xE2\x80\xA2 Select several MOD files to put them in one scene."
            "\n\xE2\x80\xA2 \xE2\x8B\xAE holds the companions: model parts, PTX, animations, room, stage config."
            "\n\xE2\x80\xA2 Hold a joint or a stage marker to see what it is; \xE2\x9C\x88 flies the camera on stages."
            "\n\xE2\x80\xA2 UV maps keep their texture's proportions; \xE2\x96\xB6 plays sounds.";
    return text;
}

}  // namespace dmcresource::spider::actions
