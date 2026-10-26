#pragma once
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>

namespace evrm {
// One registry per device, fed successful ordinary Vulkan operations only.
// BindBufferMemory2 callers must reject unsupported binding pNext semantics.
// A true result
// proves direct backing intervals disjoint, never ownership or absence of copies.
// No application pointers/resources are retained or leased.
// Up to 4096 memory and 4096 buffer records; tracked buffers are <=1 TiB.
// Overflow/metadata failure disables all future proofs for this object.
class NativeBufferMemory {
public:
    NativeBufferMemory() noexcept = default;
    void memory_created(VkDeviceMemory,const VkMemoryAllocateInfo&) noexcept;
    void memory_destroyed(VkDeviceMemory) noexcept;
    void buffer_created(VkBuffer,const VkBufferCreateInfo&) noexcept;
    void buffer_destroyed(VkBuffer) noexcept;
    void bound(VkBuffer,VkDeviceMemory,VkDeviceSize) noexcept;
    bool valid(VkBuffer,VkDeviceSize,VkDeviceSize) const noexcept;
    bool disjoint(VkBuffer,VkDeviceSize,VkDeviceSize,VkBuffer,VkDeviceSize,VkDeviceSize) const noexcept;
private:
    struct Memory {VkDeviceSize size=0;std::uint64_t generation=0;};
    struct Buffer {
        VkDeviceSize size=0,offset=0;
        std::uint64_t generation=0,memory_generation=0;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        bool attempted=false,valid=false;
    };
    struct Range {VkDeviceMemory memory{};std::uint64_t generation=0;VkDeviceSize begin=0,end=0;};
    // Caller holds mutex_. Shared by valid() and disjoint().
    bool current_range(VkBuffer,VkDeviceSize,VkDeviceSize,Range&) const noexcept;
    mutable std::mutex mutex_;
    mutable std::atomic<bool> disabled_{false};
    std::uint64_t generation_=0;
    std::map<VkDeviceMemory,Memory> memories_;
    std::map<VkBuffer,Buffer> buffers_;
};
}
