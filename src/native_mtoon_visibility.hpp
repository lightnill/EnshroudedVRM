#pragma once
#include "device_features.hpp"
#include "native_state.hpp"
#include <limits>

namespace evrm {
// Separate optional ABI. This preparation is not a pose, scene, submission or
// resource-lifetime permit. The caller still proves those before recording.
struct alignas(16) NativeMToonVisibilityUniform {
    std::uint32_t width=0,height=0,first=0,triangles=0;
};
static_assert(sizeof(NativeMToonVisibilityUniform)==16);
struct NativeMToonVisibilityPlan {
    NativeMToonVisibilityUniform uniform;
    VkDeviceSize bytes=0;
    explicit operator bool()const noexcept{return bytes!=0;}
};
inline std::uint32_t native_mtoon_surface_key(std::uint32_t first,std::uint32_t primitive,
    std::uint32_t triangles,bool front)noexcept{
    constexpr auto last=std::uint32_t(0x7fffffffu);
    if(!triangles||primitive>=triangles||triangles>last||first>last-triangles)return 0;
    return ((first+primitive+1u)<<1u)|std::uint32_t(front);
}
inline std::uint32_t native_mtoon_surface_ordinal(std::uint32_t key)noexcept{
    const auto rank=key>>1u;return rank?rank-1u:UINT32_MAX;
}
inline bool native_mtoon_fragment_identity_supported(const DeviceFeaturesObservation& f)noexcept{
    return f.complete&&f.status==DeviceFeatureStatus::ok&&f.single_physical_device&&
        f.fragment_stores_and_atomics.available&&f.fragment_stores_and_atomics.enabled&&
        f.geometry_shader.available&&f.geometry_shader.enabled;
}
inline NativeMToonVisibilityPlan make_native_mtoon_visibility_plan(const NativePipelineState& p,
    const DeviceFeaturesObservation& features,const VkPhysicalDeviceLimits& limits,VkExtent2D extent,
    std::uint32_t first,std::uint32_t triangles)noexcept{
    if(!native_mtoon_fragment_identity_supported(features)||
        !p.supported||p.kind!=NativePipelineKind::gbuffer||!p.pipeline||!p.layout||!p.generation||p.graphics_stage_count!=2||
        p.compute_bytes||p.mesh_bytes||p.tess_control_bytes||p.tess_evaluation_bytes||
        !extent.width||!extent.height||extent.width>limits.maxFramebufferWidth||extent.height>limits.maxFramebufferHeight||
        !native_mtoon_surface_key(first,0,triangles,false))return {};
    const auto pixels=std::uint64_t(extent.width)*extent.height;
    if(pixels>UINT32_MAX/4u||pixels*4u>limits.maxStorageBufferRange)return {};
    return {{extent.width,extent.height,first,triangles},pixels*4u};
}
}
