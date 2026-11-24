#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
namespace evrm {
// Per-draw owned control. The parent owns provenance/initialization, exact
// current sources, source-use/layout/queue/lifetime and submission admission.
// These words grant none of that authority by themselves. All borrowed set2
// bindings require already-enabled PARTIALLY_BOUND, and every actual access
// remains behind the coherent active/generation gate. No post-bind rewrites.
struct alignas(16) NativeMToonShadeControl {
    std::array<std::uint32_t,4> domain{}; // width,height,generation,permitted generation
    std::array<std::uint32_t,4> grants{}; // active,7-buffer mask,14-image mask,7-sampler mask
    std::array<std::uint32_t,4> draw{}; // work index,outline0/1,legacy source0/1/2,outline cull0/1/2
    std::array<std::uint32_t,4> extra{}; // water mask,original quad qualification0/1,owned diagnostic grid0/1,reserved0
};
static_assert(sizeof(NativeMToonShadeControl)==64&&alignof(NativeMToonShadeControl)==16);
static_assert(offsetof(NativeMToonShadeControl,grants)==16&&offsetof(NativeMToonShadeControl,draw)==32&&offsetof(NativeMToonShadeControl,extra)==48);
inline std::array<VkDescriptorSetLayoutBinding,3> native_mtoon_shade_material_bindings()noexcept{
    return {{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,11,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {2,VK_DESCRIPTOR_TYPE_SAMPLER,11,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
}
inline std::array<VkDescriptorSetLayoutBinding,14> native_mtoon_shade_owned_bindings()noexcept{
    std::array<VkDescriptorSetLayoutBinding,14> result{};
    for(unsigned i=0;i<result.size();++i)result[i]={i,i<3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    return result;
}
inline std::array<VkDescriptorSetLayoutBinding,26> native_mtoon_shade_scene_bindings()noexcept{
    std::array<VkDescriptorSetLayoutBinding,26> result{};
    for(unsigned i=0;i<result.size();++i){const auto type=i<4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i<19?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;
        result[i]={i,type,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};}
    return result;
}
}
