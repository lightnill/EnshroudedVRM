#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace evrm {
struct NativeVolumeTarget {
    VkImage image=VK_NULL_HANDLE;
    std::uint64_t image_generation=0;
    VkExtent3D extent{};
};
// Necessary, insufficient same-primary dependency proof. Caller serializes this
// object and supplies exact producer/witness identities, current cells/backing,
// exclusive native-source lifetime/cross-queue gates and all unobserved effects.
// No descriptor, layout-readiness lease, resource ownership or permit authority.
class NativeVolumeUse {
public:
    static constexpr std::uint32_t image_limit=512,memory_limit=128;
    void reset() noexcept;
    // IDs must increase even when the same command-buffer handle is reused.
    bool begin(std::uint64_t recording_id,VkCommandBuffer) noexcept;
    bool populate(const std::array<NativeVolumeTarget,2>&,std::uint32_t x,std::uint32_t y,std::uint32_t z) noexcept;
    bool in_scatter(std::uint32_t x,std::uint32_t y,std::uint32_t z) noexcept;
    void legacy(VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags,
                std::uint32_t memory_count,const VkMemoryBarrier*,std::uint32_t buffer_count,const VkBufferMemoryBarrier*,
                std::uint32_t image_count,const VkImageMemoryBarrier*) noexcept;
    void sync2(const VkDependencyInfo*) noexcept;
    void unobserved() noexcept;
    bool can_sample() const noexcept;
    bool insert(bool succeeded=true) noexcept;
    void end(VkResult) noexcept;
    bool complete() const noexcept;
    bool failed() const noexcept { return failed_; }
private:
    struct Image {NativeVolumeTarget target{};bool read_only=false,compute_ready=false,fragment_ready=false;};
    std::array<Image,2> images_{};
    std::uint64_t last_id_=0;
    VkCommandBuffer command_=VK_NULL_HANDLE;
    bool begun_=false,selected_=false,failed_=false,scattered_=false,inserted_=false,ended_=false;
    bool coverage(std::uint32_t,std::uint32_t,std::uint32_t)const noexcept;
    bool observing() noexcept;
    void memory(std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t) noexcept;
    void image(VkImage,VkImageLayout,VkImageLayout,std::uint32_t,std::uint32_t,const VkImageSubresourceRange&,
               std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t) noexcept;
};
}
