#pragma once
#include "native_draw.hpp"
namespace evrm {
// Owns the Basic pipeline and an optional Enhanced variant. Caller proves device/format/layout validity,
// matching viewport/scissor/stencil state and retains every source/layout/shader
// until command completion. No waits, native resource lease or activation gate.
class NativeOitDrawPipelines {
public:
    NativeOitDrawPipelines(VkDevice,PFN_vkGetDeviceProcAddr,VkPipelineLayout,VkShaderModule vertex,VkShaderModule fragment,bool independent_blend_enabled=false);
    ~NativeOitDrawPipelines();
    NativeOitDrawPipelines(const NativeOitDrawPipelines&)=delete;
    NativeOitDrawPipelines& operator=(const NativeOitDrawPipelines&)=delete;
    // Install at most one successful variant; failures preserve Basic and may
    // be retried. Caller serializes install/record and retains the layout.
    bool install_surface_variant(VkPipelineLayout,VkShaderModule vertex,VkShaderModule fragment) noexcept;
    bool surface_ready() const noexcept;
    bool record(VkCommandBuffer,const NativeDrawCall*,std::uint32_t,VkPipeline native_pipeline,VkPipelineLayout compatible_layout,std::uint32_t native_set_count,const VkDescriptorSet*) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
