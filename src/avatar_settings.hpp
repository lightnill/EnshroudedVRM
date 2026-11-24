#pragma once
#include "avatar_appearance.hpp"
#include <filesystem>
#include <optional>
namespace evrm {
struct AvatarMaterialOverride {std::uint32_t index=0;std::string name;AvatarShaderPreset preset=AvatarShaderPreset::GameLighting;};
struct AvatarOutlineOverride {std::uint32_t index=0;std::string name;AvatarOutlineColor color=AvatarOutlineColor::Black;};
struct AvatarProfile {std::string fingerprint;AvatarShaderPreset whole=AvatarShaderPreset::MToon;std::vector<AvatarMaterialOverride> overrides;std::vector<AvatarOutlineOverride> outline_colors{};};
struct AvatarPreferences {std::string selected_path;std::vector<std::string> recent;std::vector<AvatarProfile> profiles;bool replacement_enabled=true;};
enum class AvatarSettingsStatus {Missing,Loaded,Invalid,Unavailable,UnsupportedVersion};
struct AvatarSettingsLoad {AvatarPreferences preferences;AvatarSettingsStatus status=AvatarSettingsStatus::Unavailable;};
// Dropdown history is separate from appearance/assignment files for rollback compatibility.
struct AvatarDropdownPreferences {bool remembered_only=false;std::vector<std::string> paths;};
struct AvatarDropdownLoad {AvatarDropdownPreferences preferences;AvatarSettingsStatus status=AvatarSettingsStatus::Unavailable;};
AvatarDropdownLoad read_avatar_dropdown(const std::filesystem::path&)noexcept;
bool write_avatar_dropdown(const std::filesystem::path&,const AvatarDropdownPreferences&,bool replace)noexcept;
bool valid_avatar_fingerprint(const std::string&)noexcept;
bool valid_avatar_path(const std::string&,bool allow_empty=false)noexcept;
bool valid_avatar_preferences(const AvatarPreferences&)noexcept;
AvatarSettingsLoad read_avatar_preferences(const std::filesystem::path&)noexcept;
bool write_avatar_preferences(const std::filesystem::path&,const AvatarPreferences&)noexcept;
bool create_avatar_preferences(const std::filesystem::path&,const AvatarPreferences&)noexcept;
bool set_avatar_preset(AvatarPreferences&,const std::string&,AvatarShaderPreset);
bool set_avatar_material(AvatarPreferences&,const std::string&,std::uint32_t,const std::string&,std::optional<AvatarShaderPreset>);
bool set_avatar_outline_color(AvatarPreferences&,const std::string&,std::uint32_t,const std::string&,AvatarOutlineColor);
bool reset_avatar_materials(AvatarPreferences&,const std::string&);
AvatarAppearance resolve_avatar_appearance(const AvatarPreferences&,const std::string&,const std::vector<std::string>&,std::uint64_t);
}
