#pragma once
#include "mtoon_material.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace evrm {
// Independent optional ABI. The existing NativeMaterial/NativeSurfaceMaterial
// blocks and their consumers are unchanged. See shaders/mtoon_material.hlsli.
struct alignas(16) MToonGpuTexture {
    std::array<float,4> offset_scale{},rotation{};
    std::array<std::uint32_t,4> info{}; // UV set, present, source texture, sRGB
};
struct alignas(16) MToonGpuMaterial {
    std::array<float,4> base{},shade{},emission{},matcap{},rim{},outline{};
    std::array<float,4> shading{},indirect{},rim_settings{},outline_settings{},animation{};
    std::array<std::uint32_t,4> modes{},flags{};
    std::array<std::int32_t,4> queue{};
    std::array<MToonGpuTexture,vrm::mtoon_texture_count> textures{};
};
static_assert(std::is_standard_layout_v<MToonGpuMaterial>&&std::is_trivially_copyable_v<MToonGpuMaterial>);
static_assert(sizeof(MToonGpuTexture)==48&&offsetof(MToonGpuTexture,info)==32);
static_assert(alignof(MToonGpuMaterial)==16&&sizeof(MToonGpuMaterial)==752);
static_assert(offsetof(MToonGpuMaterial,shading)==96&&offsetof(MToonGpuMaterial,modes)==176);
static_assert(offsetof(MToonGpuMaterial,flags)==192&&offsetof(MToonGpuMaterial,queue)==208&&offsetof(MToonGpuMaterial,textures)==224);
inline bool mtoon_texture_srgb(vrm::MToonVersion version,vrm::MToonTextureSlot slot)noexcept{
    // UniVRM's legacy importer makes every legacy map except normals sRGB.
    // VRM1 specifies linear data channels for width/animation/shift masks.
    if(version==vrm::MToonVersion::Vrm0)return slot!=vrm::MToonTextureSlot::Normal&&slot<vrm::MToonTextureSlot::ShadingShift;
    return slot==vrm::MToonTextureSlot::Base||slot==vrm::MToonTextureSlot::Shade||slot==vrm::MToonTextureSlot::Emission||slot==vrm::MToonTextureSlot::Rim||slot==vrm::MToonTextureSlot::Matcap;
}
inline MToonGpuMaterial pack_mtoon_material(const vrm::MToonMaterial& m){
    const auto require=[](bool ok){if(!ok)throw std::invalid_argument("Invalid MToon GPU material");};
    require(m.version==vrm::MToonVersion::Vrm0||m.version==vrm::MToonVersion::Vrm1);
    require(static_cast<unsigned>(m.render_mode)<=3&&static_cast<unsigned>(m.cull_mode)<=2&&static_cast<unsigned>(m.outline_mode)<=2&&m.outline_color_mode<=1);
    MToonGpuMaterial out;
    out.base=m.base_color;
    std::copy(m.shade_color.begin(),m.shade_color.end(),out.shade.begin());
    std::copy(m.emission_color.begin(),m.emission_color.end(),out.emission.begin());
    std::copy(m.matcap_color.begin(),m.matcap_color.end(),out.matcap.begin());
    std::copy(m.rim_color.begin(),m.rim_color.end(),out.rim.begin());
    std::copy(m.outline_color.begin(),m.outline_color.end(),out.outline.begin());
    out.shading={m.shade_shift,m.shade_toony,m.receive_shadow,m.shading_grade};
    out.indirect={m.light_color_attenuation,m.indirect_intensity,m.gi_equalization,m.normal_scale};
    out.rim_settings={m.rim_lighting_mix,m.rim_power,m.rim_lift,m.shade_shift_texture_scale};
    out.outline_settings={m.outline_width,m.outline_lighting_mix,m.outline_max_distance,m.alpha_to_coverage?1.f:0.f};
    out.animation={m.uv_scroll[0],m.uv_scroll[1],m.uv_rotation,m.alpha_cutoff};
    for(const auto* values:{&out.base,&out.shade,&out.emission,&out.matcap,&out.rim,&out.outline,&out.shading,&out.indirect,&out.rim_settings,&out.outline_settings,&out.animation})
        for(float value:*values)require(std::isfinite(value));
    out.modes={1,static_cast<std::uint32_t>(m.version),static_cast<std::uint32_t>(m.render_mode),static_cast<std::uint32_t>(m.cull_mode)};
    out.flags={static_cast<std::uint32_t>(m.outline_mode),m.outline_color_mode,m.depth_write?1u:0u,m.transparent_with_zwrite?1u:0u};
    out.queue={m.render_queue_offset,m.legacy_render_queue,m.source_blend,m.destination_blend};
    for(std::size_t i=0;i<m.textures.size();++i){const auto& t=m.textures[i];auto& g=out.textures[i];
        require(t.texture>=-1&&t.texcoord>=0&&std::isfinite(t.rotation));
        for(float value:t.offset)require(std::isfinite(value));for(float value:t.scale)require(std::isfinite(value));
        g.offset_scale={t.offset[0],t.offset[1],t.scale[0],t.scale[1]};g.rotation={std::cos(t.rotation),std::sin(t.rotation),0,0};
        g.info={static_cast<std::uint32_t>(t.texcoord),t.texture>=0?1u:0u,t.texture>=0?static_cast<std::uint32_t>(t.texture):0u,mtoon_texture_srgb(m.version,static_cast<vrm::MToonTextureSlot>(i))?1u:0u};
    }
    return out;
}
}
