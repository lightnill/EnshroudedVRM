#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace evrm {
struct NativeDispatchObservation;
// Normalized current inputs supplied under the native source guard. The caller
// proves the exact producer/set0/root ABI, source backing and lifetimes, queue,
// immediate post-producer placement and completion-based retention. For indirect
// replay, argument bytes must stay unchanged until the replay reads them; buffer
// generation and the CPU source guard alone do not prove GPU content stability.
// These raw handles/capacities alone do not establish any of those facts.
struct NativeMToonProbeCopySource {
    VkDescriptorSetLayout native_layout{};
    VkDescriptorSet native_set{};
    std::array<std::uint32_t,2> list_capacity{};
    // Optional owned 16-byte UBO. Requires EVRM_MTOON_SOURCE_PERMIT=1 shader.
    // Keep it alive and immutable while pending; word0 is the submit-time permit.
    VkDescriptorBufferInfo source_permit{};
};
struct NativeMToonProbeCopyCapabilities {
    VkPhysicalDeviceLimits limits{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkFormatFeatureFlags coefficient_format_features=0;
};
// Owned scratch for ONE native update's selected cells. No source object is
// owned or modified. Successful recording is neither completion, usable GI
// coverage nor MToon scene/readiness admission.
class NativeMToonProbeCopy {
public:
    static constexpr VkDeviceSize memory_byte_limit=VkDeviceSize(4)*1024*1024;
    NativeMToonProbeCopy(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonProbeCopySource&,
        const NativeMToonProbeCopyCapabilities&,const std::uint32_t* spirv,std::size_t word_count);
    ~NativeMToonProbeCopy();
    NativeMToonProbeCopy(const NativeMToonProbeCopy&)=delete;
    NativeMToonProbeCopy& operator=(const NativeMToonProbeCopy&)=delete;
    bool record(VkCommandBuffer,VkPipeline original,VkPipelineLayout compatible_restore,
                const NativeDispatchObservation&,std::uint32_t selector)noexcept;
    // An owned range after recording, not a GPU-completion/readiness token.
    VkDescriptorBufferInfo output()const noexcept;
    // Caller retains this object until proven completion. Unknown completion
    // quarantines it without Vulkan teardown; device destruction reclaims it.
    void quarantine()noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
