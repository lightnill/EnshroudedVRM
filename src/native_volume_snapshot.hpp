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
    // Source R32_UINT/R16_UNORM, output R32_UINT/R32F, sampled RGB9E5 alias.
    std::array<VkFormatFeatureFlags,5> formats{};
    // Actual queried 3D sampled|storage mutable R32_UINT and R32F properties.
    std::array<VkImageFormatProperties,2> outputs{};
};
// Private parts of retired snapshots, kept for later snapshots on one device.
// Program: the source-free set layout, pipeline layout, shader module and
// compute pipeline, built once and held until this object and every snapshot
// using it are gone. Storage: the two owned output images with their memory,
// views, descriptor pool and set. A snapshot offers its storage here only where
// it previously destroyed it, so idle storage has no recorded or submitted
// consumer. At most idle_limit storages are idle; one that stays idle longer
// than idle_milliseconds is destroyed by release_expired(). Storage is reissued
// only for the exact extent. Its output contents are never reused: record()
// discards them (UNDEFINED layout) and the full Populate copy rewrites every
// voxel. Views of the native source images are never retained. Each storage was
// admitted under NativeVolumeSnapshot::memory_byte_limit on actual allocations.
// Serialized by the caller with snapshot construction and recording.
class NativeVolumeSnapshotReuse {
public:
    static constexpr std::size_t idle_limit=1;
    static constexpr std::uint64_t idle_milliseconds=2000;
    using Clock=std::uint64_t(*)()noexcept;
    // Sticky uncertain completion of the parent abandons everything here
    // without any Vulkan call, like every other private object.
    explicit NativeVolumeSnapshotReuse(const bool* parent_quarantine,Clock clock=nullptr);
    ~NativeVolumeSnapshotReuse();
    NativeVolumeSnapshotReuse(const NativeVolumeSnapshotReuse&)=delete;
    NativeVolumeSnapshotReuse& operator=(const NativeVolumeSnapshotReuse&)=delete;
    // false destroys idle storage now and keeps none until true again.
    void retain(bool)noexcept;
    void release_expired()noexcept;
    std::size_t idle_storages()const noexcept;
    VkDeviceSize idle_bytes()const noexcept;
    // Read-only and never waits. False when the idle holder is busy, and then
    // both outputs are zero. On true both values come from one lock.
    bool try_idle(std::size_t& storages,VkDeviceSize& bytes)const noexcept;
#ifdef EVRM_PRIVATE_REUSE_TESTING
    // Fixtures only; no product build defines the macro. Runs `inside` with
    // the idle holder's lock held, standing in for a busy keep, take or drain.
    void test_hold_idle(void(*inside)(void*),void* context)const;
#endif
    // Snapshots completed with new storage and with reissued storage.
    std::uint64_t created_storages()const noexcept;
    std::uint64_t reissued_storages()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    friend class NativeVolumeSnapshot;
};
// One snapshot per primary recording. Caller proves exact fully covering native
// Populate and records this immediately after it, before ANY next native
// command; maintains current ordinary sources/backing under its source guard;
// and retains every object until completion. No activation or commit writes.
class NativeVolumeSnapshot {
public:
    static constexpr VkDeviceSize memory_byte_limit=VkDeviceSize(64)*1024*1024;
    // Preserve native packed radiance bits plus the original R32F visibility.
    static constexpr std::uint64_t voxel_limit=memory_byte_limit/8;
    NativeVolumeSnapshot(VkDevice,PFN_vkGetDeviceProcAddr,
        const std::array<NativeVolumeSnapshotSource,2>&,const NativeVolumeSnapshotCapabilities&,
        const std::uint32_t* spirv,std::size_t word_count,std::shared_ptr<NativeVolumeSnapshotReuse> reuse={});
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
