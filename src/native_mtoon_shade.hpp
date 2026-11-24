#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "native_mtoon_shade_abi.hpp"
#include <memory>
namespace evrm {
struct NativeMToonShadeParameters {
    std::uint32_t width=0,height=0,generation=0,work_index=0,forward_capacity=0;
};
struct NativeMToonShadeShader {const std::uint32_t* words=nullptr;std::size_t count=0;};
struct NativeMToonShadeCapabilities {
    VkPhysicalDeviceLimits limits{};
    // Original device-create observation, not physical support or permission
    // to enable a feature on the game's device.
    bool descriptor_binding_partially_bound_enabled=false;
    bool forward=false;
};
struct NativeMToonShadeInputs {
    VkDescriptorBufferInfo material{};
    std::array<VkDescriptorImageInfo,11> material_maps{};
    std::array<VkSampler,11> material_samplers{};
    std::array<VkDescriptorBufferInfo,14> owned{};
    std::array<VkDescriptorBufferInfo,8> scene_buffers{};
    std::array<VkDescriptorImageInfo,11> scene_images{};
    std::array<VkSampler,7> scene_samplers{};
    VkDescriptorBufferInfo forward_constants{};VkDescriptorImageInfo forward_visibility{};VkSampler forward_sampler{};
};
// Source-free pipeline program, prepared by the parent's worker/cache away
// from rendering. Its descriptors/pipeline carry no scene or submit authority.
// The parent pointer is an identity join only; workers never read that mutable
// bool. Passes/cache propagate uncertain retirement to the atomic quarantine.
class NativeMToonShadeProgram {
public:
    NativeMToonShadeProgram(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonShadeCapabilities&,
        NativeMToonShadeShader,const bool* parent_quarantine);
    ~NativeMToonShadeProgram();
    // Serialized owner withdraws uncertain device completion before release.
    void quarantine()const noexcept;
    bool reusable()const noexcept;
    NativeMToonShadeProgram(const NativeMToonShadeProgram&)=delete;
    NativeMToonShadeProgram& operator=(const NativeMToonShadeProgram&)=delete;
private:
    struct Impl;std::unique_ptr<Impl> impl;
    friend class NativeMToonShadePass;
};
// Host pass construction/recording/destruction is externally serialized. The
// program retains bounded dormant descriptor banks (and composite storage).
// Parent must prove original GPU completion/reset before releasing a pass;
// only a bank whose final pass lease retired can be rewritten/reused.
// One original recording's descriptor owner. Every descriptor is written
// before binding. Missing optional roles need valid typed owned fallbacks;
// masks do not make invalid nonpartial descriptors legal. PARTIALLY_BOUND on
// all borrowed bindings supports complete gate withdrawal, not source admission.
// The parent proves original source initialization/use/layout/queue/lifetime,
// physical disjointness and immutable descriptor provenance; owns bounded work,
// control/packet generation and source masks; serializes host access and keeps
// inputs, original compatible pipeline/set prefix, device and quarantine alive
// through retirement. Native output images are not descriptors of this pass.
class NativeMToonShadePass {
public:
    static constexpr std::uint32_t pixel_limit=1u<<24,draw_limit=8192;
    NativeMToonShadePass(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonShadeCapabilities&,
        NativeMToonShadeParameters,const NativeMToonShadeInputs&,const bool* parent_quarantine,
        std::shared_ptr<const NativeMToonShadeProgram> prepared);
    ~NativeMToonShadePass();
    NativeMToonShadePass(const NativeMToonShadePass&)=delete;
    NativeMToonShadePass& operator=(const NativeMToonShadePass&)=delete;
    // No allocations, pipeline compilation, image transitions, push writes,
    // submit or wait. Restores the whole admitted original compute set prefix.
    bool record(VkCommandBuffer,VkPipeline original,VkPipelineLayout compatible,
        std::uint32_t native_set_count,const VkDescriptorSet*)noexcept;
    void quarantine()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
