#pragma once
#include "avatar_settings.hpp"
namespace evrm {
enum class AvatarLoadState:std::uint8_t {Ready,Loading,Preparing,Choosing,Error};
struct AvatarChoice {std::string path,name;};
struct AvatarMenuSnapshot {
    AvatarLoadState state=AvatarLoadState::Ready;
    bool preferences_loaded=false,save_failed=false;
    std::uint64_t revision=0;
    std::string name,path,fingerprint,error;
    std::shared_ptr<const std::vector<AvatarChoice>> choices;
    std::shared_ptr<const std::vector<std::string>> materials;
    std::shared_ptr<const AvatarAppearance> appearance;
    std::shared_ptr<const AvatarProfile> profile;
};
enum class AvatarActionKind:std::uint8_t {None,SelectModel,Browse,SetWholePreset,SetMaterialPreset,ResetMaterials,CancelLoad,RefreshCatalogue};
struct AvatarMenuAction {
    AvatarActionKind kind=AvatarActionKind::None;
    std::string path,fingerprint;std::uint32_t material=0;
    std::optional<AvatarShaderPreset> preset;
};
struct AvatarPickResult {std::string path,error;bool cancelled=false;};
}
