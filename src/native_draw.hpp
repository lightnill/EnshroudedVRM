#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace evrm {
struct DeviceFeaturesObservation;
struct NativeDrawCall {
    VkDescriptorSet set{};
    std::uint32_t first_index=0,index_count=0;
    VkDescriptorSet surface_set{};
    // Optional set2: frame-owned visible-surface identity inputs. All draws in
    // an identity-enabled GBuffer recording must supply it. Ignored elsewhere.
    VkDescriptorSet identity_set{};
};
// Owns the private Basic pipelines and an optional Enhanced GBuffer pipeline.
// The caller retains shaders/layout,
// descriptors and geometry until the original command buffer is retired.
class NativeDrawPipelines {
public:
    NativeDrawPipelines(VkDevice,PFN_vkGetDeviceProcAddr,VkPipelineLayout,
                       VkShaderModule vertex,VkShaderModule visibility,
                       VkShaderModule gbuffer,std::uint32_t visibility_colors=2);
    ~NativeDrawPipelines();
    NativeDrawPipelines(const NativeDrawPipelines&)=delete;
    NativeDrawPipelines& operator=(const NativeDrawPipelines&)=delete;
    // Install at most one successful Enhanced variant. Invalid/failed attempts
    // leave Basic usable and may be retried. Caller serializes install/record.
    bool install_surface_variant(VkPipelineLayout,VkShaderModule vertex,VkShaderModule fragment) noexcept;
    bool surface_ready() const noexcept;
    // Independent optional GBuffer instrumentation. These pipelines consume
    // the already enabled fragment-storage/primitive-ID features; never enable
    // device features here. Caller retains set0/set2 layouts, shaders and inputs.
    bool install_identity_variant(bool surface,VkPipelineLayout,VkShaderModule vertex,VkShaderModule fragment,
        const DeviceFeaturesObservation&) noexcept;
    bool identity_ready(bool surface)const noexcept;
    // Requires matching native viewport/scissor/stencil dynamic state. Changes
    // only graphics pipeline/descriptors, restoring the complete native sets
    // through a retained layout with identical set definitions/push ranges.
    bool record(VkCommandBuffer,bool gbuffer,const NativeDrawCall*,std::uint32_t,
                VkPipeline native_pipeline,VkPipelineLayout compatible_layout,
                std::uint32_t native_set_count,const VkDescriptorSet*) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Private depth-only caster. Geometry/materials are the loaded avatar's existing
// draw sets; set1 is an immutable retained native ShadowUBO view. No ownership
// or common-commit authority is established by this command recorder.
class NativeShadowDrawPipeline {
public:
    NativeShadowDrawPipeline(VkDevice,PFN_vkGetDeviceProcAddr,VkPipelineLayout,VkShaderModule vertex,VkShaderModule fragment);
    ~NativeShadowDrawPipeline();
    NativeShadowDrawPipeline(const NativeShadowDrawPipeline&)=delete;
    NativeShadowDrawPipeline& operator=(const NativeShadowDrawPipeline&)=delete;
    bool record(VkCommandBuffer,VkDescriptorSet view,const NativeDrawCall*,std::uint32_t,
                VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
