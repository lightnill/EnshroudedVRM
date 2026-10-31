#pragma once
#include "avatar_appearance.hpp"
#include <filesystem>
#include <optional>
namespace evrm {
struct AvatarMaterialOverride {std::uint32_t index=0;std::string name;AvatarShaderPreset preset=AvatarShaderPreset::GameLighting;};
struct AvatarProfile {std::string fingerprint;AvatarShaderPreset whole=AvatarShaderPreset::GameLighting;std::vector<AvatarMaterialOverride> overrides;};
struct AvatarPreferences {std::string selected_path;std::vector<std::string> recent;std::vector<AvatarProfile> profiles;};
enum class AvatarSettingsStatus {Missing,Loaded,Invalid,Unavailable,UnsupportedVersion};
struct AvatarSettingsLoad {AvatarPreferences preferences;AvatarSettingsStatus status=AvatarSettingsStatus::Unavailable;};
bool valid_avatar_fingerprint(const std::string&)noexcept;
bool valid_avatar_path(const std::string&,bool allow_empty=false)noexcept;
bool valid_avatar_preferences(const AvatarPreferences&)noexcept;
AvatarSettingsLoad read_avatar_preferences(const std::filesystem::path&)noexcept;
bool write_avatar_preferences(const std::filesystem::path&,const AvatarPreferences&)noexcept;
bool set_avatar_preset(AvatarPreferences&,const std::string&,AvatarShaderPreset);
bool set_avatar_material(AvatarPreferences&,const std::string&,std::uint32_t,const std::string&,std::optional<AvatarShaderPreset>);
bool reset_avatar_materials(AvatarPreferences&,const std::string&);
AvatarAppearance resolve_avatar_appearance(const AvatarPreferences&,const std::string&,const std::vector<std::string>&,std::uint64_t);
}
