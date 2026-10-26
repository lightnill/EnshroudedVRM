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
// Owned normalized facts supplied from current ordinary native metadata. The
// manager cannot validate producer identity, execution, allocation provenance,
// lifetime or synchronization with other queues/host from these scalars.
struct NativeVolumeSnapshotSource {
    VkImage image{};std::uint64_t generation=0;
    VkImageType type=VK_IMAGE_TYPE_3D;VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageCreateFlags flags=0;VkImageUsageFlags usage=0;VkExtent3D extent{};
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    std::uint32_t mip_levels=1,array_layers=1,format_count=0;
    std::array<VkFormat,16> formats{};
};
struct NativeVolumeSnapshotCapabilities {
    VkPhysicalDeviceLimits limits{};
    VkPhysicalDeviceMemoryProperties memory{};
    // E5B9G9R9, R16_UNORM, RGBA32F, R32F: optimal tiling features.
    std::array<VkFormatFeatureFlags,4> formats{};
    // Actual queried 3D sampled|storage RGBA32F and R32F properties.
    std::array<VkImageFormatProperties,2> outputs{};
};
// One snapshot per primary recording. Caller proves exact fully covering native
// Populate and records this immediately after it, before ANY next native
// command; maintains current ordinary sources/backing under its source guard;
// and retains every object until completion. No activation or commit writes.
class NativeVolumeSnapshot {
public:
    static constexpr std::uint64_t voxel_limit=1048576;
    static constexpr VkDeviceSize memory_byte_limit=VkDeviceSize(64)*1024*1024;
    NativeVolumeSnapshot(VkDevice,PFN_vkGetDeviceProcAddr,
        const std::array<NativeVolumeSnapshotSource,2>&,const NativeVolumeSnapshotCapabilities&,
        const std::uint32_t* spirv,std::size_t word_count);
    ~NativeVolumeSnapshot();
    NativeVolumeSnapshot(const NativeVolumeSnapshot&)=delete;
    NativeVolumeSnapshot& operator=(const NativeVolumeSnapshot&)=delete;
    bool record(VkCommandBuffer,VkPipeline native_pipeline,VkPipelineLayout compatible_layout,
        std::uint32_t native_set_count,const VkDescriptorSet*,std::uint32_t gx,std::uint32_t gy,std::uint32_t gz) noexcept;
    std::array<VkDescriptorImageInfo,2> outputs() const noexcept;
    // Sticky: no Vulkan teardown under uncertain completion. Device destruction
    // ultimately reclaims quarantined objects; this never waits or resubmits.
    void quarantine() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
