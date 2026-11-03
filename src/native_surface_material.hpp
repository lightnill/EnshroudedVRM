#pragma once
#include "avatar_appearance.hpp"
#include "vrm_asset.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evrm {
enum NativeSurfaceFeature:std::uint32_t {surface_normal=1,surface_matcap=2,surface_rim=4,surface_mask=8};
struct NativeSurfaceSelection {
    std::vector<std::uint32_t> features; // One per material, then an empty fallback.
    std::uint32_t combined=0;
    bool any()const noexcept{return combined!=0;}
    bool needs_vertices()const noexcept{return (combined&(surface_normal|surface_mask))!=0;}
    bool needs_tangents()const noexcept{return (combined&surface_normal)!=0;}
    bool operator==(const NativeSurfaceSelection& b)const noexcept{return combined==b.combined&&features==b.features;}
    bool operator!=(const NativeSurfaceSelection& b)const noexcept{return !(*this==b);}
};
inline bool native_surface_metadata_valid(const vrm::MaterialSurface& s)noexcept {
    if(s.kind!=vrm::SurfaceKind::Gltf&&s.kind!=vrm::SurfaceKind::MToon0&&s.kind!=vrm::SurfaceKind::MToon1)return false;
    if(!std::isfinite(s.normal_scale)||!std::isfinite(s.rim_power)||s.rim_power<0||
       !std::isfinite(s.rim_lift)||!std::isfinite(s.rim_lighting_mix)||s.rim_lighting_mix<0||s.rim_lighting_mix>1)return false;
    for(const auto* color:{&s.matcap_factor,&s.rim_color})for(const auto v:*color)
        if(!std::isfinite(v)||v<0||v>65504.f)return false;
    for(const auto* texture:{&s.normal,&s.matcap,&s.rim}){
        if(texture->texture< -1||texture->texcoord<0||!std::isfinite(texture->rotation))return false;
        for(auto value:texture->offset)if(!std::isfinite(value))return false;
        for(auto value:texture->scale)if(!std::isfinite(value))return false;
    }
    return true;
}
inline std::uint32_t native_surface_features(const vrm::Material& material)noexcept {
    if(!material.surface||!native_surface_metadata_valid(*material.surface))return 0;
    const auto& s=*material.surface;std::uint32_t flags=0;
    if(s.normal.texture>=0&&s.normal_scale!=0)flags|=surface_normal;
    if(s.kind!=vrm::SurfaceKind::Gltf){
        if(s.matcap.texture>=0&&std::any_of(s.matcap_factor.begin(),s.matcap_factor.end(),[](float v){return v>0;}))flags|=surface_matcap;
        if(std::any_of(s.rim_color.begin(),s.rim_color.end(),[](float v){return v>0;}))flags|=surface_rim;
        const bool masked=(flags&surface_rim)||(s.kind==vrm::SurfaceKind::MToon1&&(flags&surface_matcap));
        if(masked&&s.rim.texture>=0)flags|=surface_mask;
    }
    return flags;
}
inline NativeSurfaceSelection make_native_surface_selection(const vrm::Avatar& avatar,const AvatarAppearance* appearance=nullptr) {
    NativeSurfaceSelection out;out.features.reserve(avatar.materials.size()+1);
    for(std::size_t i=0;i<avatar.materials.size();++i){
        const auto preset=avatar_material_preset(appearance,i);
        const auto flags=(preset==AvatarShaderPreset::Fast||preset==AvatarShaderPreset::Matte)?0:native_surface_features(avatar.materials[i]);
        out.features.push_back(flags);out.combined|=flags;
    }
    out.features.push_back(0);return out;
}
inline bool native_surface_valid_selection(const vrm::Avatar& avatar,const NativeSurfaceSelection& selection)noexcept {
    if(selection.features.size()!=avatar.materials.size()+1||selection.features.back()!=0)return false;
    std::uint32_t combined=0;
    for(std::size_t i=0;i<avatar.materials.size();++i){
        if(selection.features[i]&~native_surface_features(avatar.materials[i]))return false;
        combined|=selection.features[i];
    }
    return combined==selection.combined;
}
// Published resources are immutable. A newly requested effect may draw only
// after its upload completes; disabling it takes effect immediately.
inline NativeSurfaceSelection native_surface_active_selection(const vrm::Avatar& avatar,
    const NativeSurfaceSelection& desired,const NativeSurfaceSelection& ready) {
    NativeSurfaceSelection out;out.features.resize(avatar.materials.size()+1);
    if(!native_surface_valid_selection(avatar,desired)||!native_surface_valid_selection(avatar,ready))return out;
    for(std::size_t i=0;i<avatar.materials.size();++i){out.features[i]=desired.features[i]&ready.features[i];out.combined|=out.features[i];}
    return out;
}
// Separate optional uniform block. The existing80-byte NativeMaterial stays intact.
struct alignas(16) NativeSurfaceMaterial {
    std::array<float,4> normal{},matcap{},rim{},params{},cap_uv0{},cap_uv1{};
    std::array<std::uint32_t,4> flags{};
};
static_assert(sizeof(NativeSurfaceMaterial)==112&&offsetof(NativeSurfaceMaterial,flags)==96,"Enhanced material uniform ABI");
inline NativeSurfaceMaterial native_surface_material(const vrm::Material& material,std::uint32_t features)noexcept {
    NativeSurfaceMaterial out;
    if(!material.surface||!features||(features&~native_surface_features(material)))return out;
    const auto& s=*material.surface;
    out.normal[0]=s.normal_scale;
    std::copy(s.matcap_factor.begin(),s.matcap_factor.end(),out.matcap.begin());
    std::copy(s.rim_color.begin(),s.rim_color.end(),out.rim.begin());
    out.params={s.rim_power,s.rim_lift,s.rim_lighting_mix,0};
    out.cap_uv0={s.matcap.scale[0],s.matcap.scale[1],s.matcap.offset[0],s.matcap.offset[1]};
    out.cap_uv1={std::cos(s.matcap.rotation),std::sin(s.matcap.rotation),0,0};
    out.flags={features,static_cast<std::uint32_t>(s.kind),0,0};return out;
}
struct NativeSurfaceBounds {std::array<float,4> lit{},unlit{};};
inline NativeSurfaceBounds native_surface_bounds(const vrm::Material& material,std::uint32_t features)noexcept {
    NativeSurfaceBounds out;
    if(!material.surface||!features||(features&~native_surface_features(material)))return out;
    const auto& s=*material.surface;
    for(unsigned i=0;i<3;++i){
        const float cap=features&surface_matcap?s.matcap_factor[i]:0;
        const float rim=features&surface_rim?s.rim_color[i]:0;
        if(s.kind==vrm::SurfaceKind::MToon0){out.lit[i]=rim*s.rim_lighting_mix;out.unlit[i]=cap+rim*(1-s.rim_lighting_mix);}
        else {out.lit[i]=(cap+rim)*s.rim_lighting_mix;out.unlit[i]=(cap+rim)*(1-s.rim_lighting_mix);}
    }
    return out;
}
}
