#pragma once
#include "material_surface.hpp"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace vrm {
enum class MToonVersion:std::uint8_t {Vrm0=0,Vrm1=1};
enum class MToonRenderMode:std::uint8_t {Opaque=0,Cutout=1,Transparent=2,TransparentZWrite=3};
enum class MToonCullMode:std::uint8_t {None=0,Front=1,Back=2};
enum class MToonOutlineMode:std::uint8_t {None=0,World=1,Screen=2};
enum class MToonTextureSlot:std::uint8_t {
    Base,Shade,Normal,ReceiveShadow,ShadingGrade,Emission,Rim,Matcap,
    OutlineWidth,UvAnimationMask,ShadingShift,Count
};
enum class MToonField:std::uint8_t {
    BaseColor,ShadeColor,ShadeShift,ShadeToony,ReceiveShadow,ShadingGrade,
    LightColorAttenuation,IndirectIntensity,GiEqualization,NormalScale,
    EmissionColor,MatcapColor,RimColor,RimLightingMix,RimPower,RimLift,
    OutlineMode,OutlineWidth,OutlineColor,OutlineLightingMix,OutlineColorMode,
    OutlineMaxDistance,UvScrollX,UvScrollY,UvRotation,AlphaCutoff,RenderMode,
    CullMode,DepthWrite,Queue,ShiftTextureScale,MainTransform,OutlineCullMode,Count
};
constexpr std::size_t mtoon_texture_count=static_cast<std::size_t>(MToonTextureSlot::Count);
// Source-version semantics are retained. This is not a GPU ABI or proof that a
// feature has been uploaded/rendered; consumers use an immutable shared record.
struct MToonMaterial {
    MToonVersion version=MToonVersion::Vrm1;
    std::array<float,4> base_color{1,1,1,1};
    std::array<float,3> shade_color{1,1,1},emission_color{},matcap_color{1,1,1},rim_color{},outline_color{};
    float shade_shift=0,shade_toony=.9f,receive_shadow=1,shading_grade=1;
    float light_color_attenuation=0,indirect_intensity=.1f,gi_equalization=.9f;
    float normal_scale=1,shade_shift_texture_scale=1;
    float rim_lighting_mix=1,rim_power=5,rim_lift=0;
    MToonOutlineMode outline_mode=MToonOutlineMode::None;
    float outline_width=0,outline_lighting_mix=1,outline_max_distance=1;
    std::uint8_t outline_color_mode=0;
    std::array<float,2> uv_scroll{};
    float uv_rotation=0,alpha_cutoff=.5f;
    MToonRenderMode render_mode=MToonRenderMode::Opaque;
    MToonCullMode cull_mode=MToonCullMode::Back,outline_cull_mode=MToonCullMode::Front;
    bool depth_write=true,transparent_with_zwrite=false,alpha_to_coverage=false;
    int render_queue_offset=0,legacy_render_queue=-1,source_blend=1,destination_blend=0;
    std::array<SurfaceTexture,mtoon_texture_count> textures{};
    std::bitset<mtoon_texture_count> texture_authored;
    std::bitset<static_cast<std::size_t>(MToonField::Count)> authored;
    bool was_authored(MToonField field)const noexcept{const auto i=static_cast<std::size_t>(field);return i<authored.size()&&authored[i];}
    const SurfaceTexture& texture(MToonTextureSlot slot)const{return textures.at(static_cast<std::size_t>(slot));}
};
}
