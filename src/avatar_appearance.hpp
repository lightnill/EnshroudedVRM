#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace evrm {
enum class AvatarShaderPreset:std::uint8_t {GameLighting,Matte};
inline bool valid_avatar_preset(AvatarShaderPreset value)noexcept{return value==AvatarShaderPreset::GameLighting||value==AvatarShaderPreset::Matte;}
struct AvatarAppearance {
    std::string fingerprint;
    std::uint64_t generation=0;
    AvatarShaderPreset whole=AvatarShaderPreset::GameLighting;
    std::vector<AvatarShaderPreset> materials;
};
inline AvatarShaderPreset avatar_material_preset(const AvatarAppearance* appearance,std::size_t index)noexcept {
    if(!appearance)return AvatarShaderPreset::GameLighting;
    const auto value=index<appearance->materials.size()?appearance->materials[index]:appearance->whole;
    return valid_avatar_preset(value)?value:AvatarShaderPreset::GameLighting;
}
inline float avatar_material_shader_mode(const AvatarAppearance* appearance,std::size_t index)noexcept {
    return avatar_material_preset(appearance,index)==AvatarShaderPreset::Matte?2.f:0.f;
}
}
