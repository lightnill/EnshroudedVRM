#pragma once
#include "native_clip_shader.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace evrm {
class NativeClipPipelines;
struct NativeClipPipeline {
    VkPipeline handle=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline original=VK_NULL_HANDLE;
    VkPipelineLayout original_layout=VK_NULL_HANDLE;
    NativeClipKind kind=NativeClipKind::none;
    bool mesh=false;
private:
    std::shared_ptr<const void> lifetime_;
    friend class NativeClipPipelines;
};
// Optional next-layer resources only. Original objects/call results are untouched.
// Leases must retire before device destruction, or quarantine() must be called.
class NativeClipPipelines {
public:
    NativeClipPipelines(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceLimits&,bool mesh_stage=false,bool shadow_probe=false) noexcept;
    ~NativeClipPipelines() noexcept;
    NativeClipPipelines(const NativeClipPipelines&)=delete;
    NativeClipPipelines& operator=(const NativeClipPipelines&)=delete;
    VkDescriptorSetLayout private_layout() const noexcept;
    void layout_created(VkDescriptorSetLayout,const VkDescriptorSetLayoutCreateInfo&) noexcept;
    void layout_destroyed(VkDescriptorSetLayout) noexcept;
    void pipeline_layout_created(VkPipelineLayout,const VkPipelineLayoutCreateInfo&) noexcept;
    void pipeline_layout_destroyed(VkPipelineLayout) noexcept;
    void shader_created(VkShaderModule,const VkShaderModuleCreateInfo&) noexcept;
    void shader_destroyed(VkShaderModule) noexcept;
    void graphics_pipelines_created(std::uint32_t,const VkGraphicsPipelineCreateInfo*,const VkPipeline*) noexcept;
    void pipeline_destroyed(VkPipeline) noexcept;
    std::shared_ptr<const NativeClipPipeline> pipeline(VkPipeline) const noexcept;
    // Bounded copied observations, including binding masks and visited push
    // range values; no application-pointer reads or driver calls. Diagnostic
    // allocation/capacity cannot change admission.
    std::string diagnostic(VkPipeline original,VkPipelineLayout original_layout) const noexcept;
    void quarantine() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
