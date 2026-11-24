#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <memory>
namespace evrm {
struct DeviceFeaturesObservation;
struct NativeMToonMainCall {
    // Owned geometry/frame, authored material, exact current draw/UV/identity.
    std::array<VkDescriptorSet,3> sets{};
    std::uint32_t first_index=0,index_count=0;VkCullModeFlags cull=VK_CULL_MODE_NONE;
};
// Native-format main visibility/GBuffer pair. Prepare off the render path;
// original enabled identity features are required and none are enabled here.
// Parent joins and retains uploaded materials, exact current/previous posed
// geometry and frame, descriptor sets/layouts/shaders and compatible original
// state through retirement, and supplies the native dynamic viewport/scissor/
// stencil state. This grants no native scene/source or common-commit authority.
// There is no outline pipeline, expanded geometry, submit or wait in this owner.
class NativeMToonMainPipelines {
public:
    NativeMToonMainPipelines(VkDevice,PFN_vkGetDeviceProcAddr,VkPipelineLayout,
        VkShaderModule vertex,VkShaderModule visibility,VkShaderModule gbuffer,
        std::uint32_t cull_modes,const DeviceFeaturesObservation&,bool forward_capture=false);
    ~NativeMToonMainPipelines();
    NativeMToonMainPipelines(const NativeMToonMainPipelines&)=delete;
    NativeMToonMainPipelines& operator=(const NativeMToonMainPipelines&)=delete;
    bool record(VkCommandBuffer,bool gbuffer,const NativeMToonMainCall*,std::uint32_t,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
    void quarantine()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
