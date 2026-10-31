#pragma once
#include "vrm_asset.hpp"
#include <algorithm>
namespace evrm {
struct alignas(16) NativeMaterial {float base[4]{},emissive[4]{},surface[4]{};std::uint32_t instance[4]{};float alpha[4]{};};
static_assert(sizeof(NativeMaterial)==80,"Native material ABI");
inline NativeMaterial native_avatar_material(const vrm::Material& source,std::uint32_t tag,float mode,bool menu_cutout)noexcept {
    NativeMaterial material;std::copy(source.base_color.begin(),source.base_color.end(),material.base);
    material.surface[1]=.35f;material.surface[3]=1;material.instance[0]=tag;material.instance[3]=1;
    material.alpha[0]=source.alpha_cutoff;material.alpha[1]=source.alpha==vrm::AlphaMode::Mask?1.f:source.alpha==vrm::AlphaMode::Blend?2.f:0.f;
    if(menu_cutout&&source.alpha==vrm::AlphaMode::Blend){material.alpha[0]=.5f;material.alpha[1]=1.f;}
    material.alpha[2]=source.double_sided?1.f:0.f;material.alpha[3]=mode;return material;
}
}
