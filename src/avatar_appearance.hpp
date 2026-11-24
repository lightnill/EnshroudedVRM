#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace evrm {
enum class AvatarShaderPreset:std::uint8_t {GameLighting=0,Matte=1,Enhanced=2,Fast=3,MToon=4,MToonOutline=5,NillToon=6,NillToonOutline=7};
inline bool valid_avatar_preset(AvatarShaderPreset value)noexcept{return value==AvatarShaderPreset::GameLighting||value==AvatarShaderPreset::Matte||value==AvatarShaderPreset::Enhanced||value==AvatarShaderPreset::Fast||value==AvatarShaderPreset::MToon||value==AvatarShaderPreset::MToonOutline||value==AvatarShaderPreset::NillToon||value==AvatarShaderPreset::NillToonOutline;}
// Retired IDs remain readable, but resolve to the supported MToon family.
inline AvatarShaderPreset canonical_avatar_preset(AvatarShaderPreset p)noexcept{return p==AvatarShaderPreset::NillToon?AvatarShaderPreset::MToon:p==AvatarShaderPreset::NillToonOutline?AvatarShaderPreset::MToonOutline:p;}
inline bool avatar_preset_is_nilltoon(AvatarShaderPreset p)noexcept{return p==AvatarShaderPreset::NillToon||p==AvatarShaderPreset::NillToonOutline;}
inline bool avatar_preset_is_toon(AvatarShaderPreset p)noexcept{return p==AvatarShaderPreset::MToon||p==AvatarShaderPreset::MToonOutline||avatar_preset_is_nilltoon(p);}
inline bool avatar_preset_has_outline(AvatarShaderPreset p)noexcept{return p==AvatarShaderPreset::MToonOutline||p==AvatarShaderPreset::NillToonOutline;}
enum class AvatarOutlineColor:std::uint8_t {Black=0,Model=1};
inline bool valid_avatar_outline_color(AvatarOutlineColor value)noexcept{return value==AvatarOutlineColor::Black||value==AvatarOutlineColor::Model;}
struct AvatarAppearance {
    std::string fingerprint;
    std::uint64_t generation=0;
    AvatarShaderPreset whole=AvatarShaderPreset::GameLighting;
    std::vector<AvatarShaderPreset> materials;
    std::vector<AvatarOutlineColor> outline_colors{};
};
inline AvatarShaderPreset avatar_material_preset(const AvatarAppearance* appearance,std::size_t index)noexcept {
    if(!appearance)return AvatarShaderPreset::GameLighting;
    const auto value=index<appearance->materials.size()?appearance->materials[index]:appearance->whole;
    return valid_avatar_preset(value)?canonical_avatar_preset(value):AvatarShaderPreset::GameLighting;
}
inline AvatarOutlineColor avatar_material_outline_color(const AvatarAppearance* appearance,std::size_t index)noexcept {
    if(!appearance||index>=appearance->outline_colors.size())return AvatarOutlineColor::Black;
    const auto value=appearance->outline_colors[index];return valid_avatar_outline_color(value)?value:AvatarOutlineColor::Black;
}
inline float avatar_material_shader_mode(const AvatarAppearance* appearance,std::size_t index)noexcept {
    return avatar_material_preset(appearance,index)==AvatarShaderPreset::Matte?2.f:0.f;
}
}
