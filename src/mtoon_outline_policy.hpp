#pragma once
#include "mtoon_policy.hpp"
#include <vector>
namespace evrm {
inline std::vector<std::size_t> mtoon_outline_materials(const vrm::Avatar& avatar,const AvatarAppearance* appearance,const std::vector<bool>& ready){
    std::vector<std::size_t> result;
    if(ready.size()!=avatar.materials.size())return result;
    for(std::size_t i=0;i<avatar.materials.size();++i)
        if(mtoon_material_selection(avatar.materials[i],avatar_material_preset(appearance,i),ready[i]).outline)result.push_back(i);
    return result;
}
inline vrm::MToonCullMode mtoon_outline_cull(const vrm::MToonMaterial& m)noexcept{
    return m.version==vrm::MToonVersion::Vrm0?m.outline_cull_mode:vrm::MToonCullMode::Front;
}
}
