#pragma once
#include "avatar_settings.hpp"
#include "avatar_player_types.hpp"
namespace evrm {
enum class AvatarLoadState:std::uint8_t {Ready,Loading,Preparing,Choosing,Error};
struct AvatarChoice {std::string path,name;};
struct AvatarMenuSnapshot {
    AvatarLoadState state=AvatarLoadState::Ready;
    bool preferences_loaded=false,save_failed=false,replacement_enabled=true;
    std::uint64_t revision=0;
    std::string name,path,fingerprint,error;
    std::uint64_t decoded_texture_bytes=0,retained_cpu_buffer_bytes=0;
    std::shared_ptr<const std::vector<AvatarChoice>> choices;
    std::shared_ptr<const std::vector<std::string>> materials;
    std::shared_ptr<const AvatarAppearance> appearance;
    std::shared_ptr<const AvatarProfile> profile;
    std::string subject{};
    std::shared_ptr<const std::vector<AvatarPlayerChoice>> players{};
};
enum class AvatarActionKind:std::uint8_t {None,SelectModel,Browse,SetWholePreset,SetMaterialPreset,ResetMaterials,CancelLoad,RefreshCatalogue,SetReplacementEnabled,SelectPlayer};
struct AvatarMenuAction {
    AvatarActionKind kind=AvatarActionKind::None;
    std::string path,fingerprint;std::uint32_t material=0;
    std::optional<AvatarShaderPreset> preset;
    bool replacement_enabled=true;
    std::string subject{};
};
struct AvatarPickResult {std::string path,error;bool cancelled=false;};
}
