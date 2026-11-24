#pragma once
#include "avatar_appearance.hpp"
#include "vrm_asset.hpp"
#include <cmath>
namespace evrm {
struct MToonMaterialSelection {bool requested=false,ready=false,outline=false;};
// resources_ready attests the renderer's versioned upload/scene contracts;
// a saved/requested preset alone never admits MToon or outline GPU work.
inline MToonMaterialSelection mtoon_material_selection(const vrm::Material& material,
    AvatarShaderPreset preset,bool resources_ready)noexcept {
    MToonMaterialSelection out;
    out.requested=avatar_preset_is_toon(preset);
    if(!out.requested||!resources_ready||!material.mtoon)return out;
    const auto& source=*material.mtoon;
    if(source.version!=vrm::MToonVersion::Vrm0&&source.version!=vrm::MToonVersion::Vrm1)return out;
    out.ready=true;
    out.outline=avatar_preset_has_outline(preset)&&std::isfinite(source.outline_width)&&source.outline_width>0&&
        (source.outline_mode==vrm::MToonOutlineMode::World||source.outline_mode==vrm::MToonOutlineMode::Screen);
    return out;
}
}
