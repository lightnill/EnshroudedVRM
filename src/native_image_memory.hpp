#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>

namespace evrm {
struct NativeImageBacking {
    VkImage image{};VkDeviceMemory memory{};
    std::uint64_t image_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,bytes=0;
    std::uint32_t memory_type_index=0;
    explicit operator bool()const noexcept{return image&&memory&&image_generation&&memory_generation&&overlap_revision&&bytes;}
};
// Allocation metadata only: competing bindings are not excluded. This is not
// exclusive backing, source readiness, a producer witness or a lifetime lease.
// Caller separately proves the exact full covering producer, immediate
// same-stage copy synchronization and source lifetimes. A fresh token after
// overlap changes does not establish that a new producer has actually run.
struct NativeImageAllocationBinding {
    VkImage image{};VkDeviceMemory memory{};
    std::uint64_t image_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,bytes=0;
    std::uint32_t memory_type_index=0;
    explicit operator bool()const noexcept{return image&&memory&&image_generation&&memory_generation&&overlap_revision&&bytes;}
};
enum class NativeImageDisableReason : std::uint32_t {
    none,invalid_configuration,generation_exhaustion,revision_exhaustion,
    allocation_census,image_census,buffer_census,metadata_exception,
    unknown_memory_or_binding,explicit_unobserved
};
// Read-only diagnostic facts, never an exclusive-backing certificate.
struct NativeImageMemoryDiagnostic {
    NativeImageDisableReason disable_reason=NativeImageDisableReason::none;
    bool observed=false,disabled=false,image_known=false,target=false,query_safe=false;
    bool bind_attempted=false,footprint_known=false,allocation_known=false,allocation_ordinary=false,allocation_uncertain=false;
    std::uint32_t allocations=0,images=0,buffers=0,memory_type=0,image_conflicts=0,buffer_conflicts=0;
    VkMemoryPropertyFlags memory_properties=0;
    VkDeviceSize granularity=0,offset=0,bytes=0,allocation_bytes=0;
    VkDeviceMemory memory{};
    std::uint64_t bound_memory_generation=0,current_memory_generation=0;
};
// Host metadata only. No driver calls, contents/layout certificate, pending
// lifetime lease or physical native destruction deferral. Caller serializes
// native source construction/retirement with the backend source guard.
class NativeImageMemory {
public:
    static constexpr std::size_t record_cap=4096;
    // Larger full-census metadata ceiling for texture-heavy native scenes.
    static constexpr std::size_t image_record_cap=65536;
    static constexpr VkDeviceSize size_cap=VkDeviceSize(1)<<40;
    NativeImageMemory(const VkPhysicalDeviceMemoryProperties&,VkDeviceSize buffer_image_granularity)noexcept;
    void memory_created(VkDeviceMemory,const VkMemoryAllocateInfo&)noexcept;
    void memory_destroyed(VkDeviceMemory)noexcept;
    void image_created(VkImage,const VkImageCreateInfo&)noexcept;
    void image_destroyed(VkImage)noexcept;
    void buffer_created(VkBuffer,const VkBufferCreateInfo&)noexcept;
    void buffer_destroyed(VkBuffer)noexcept;
    bool image_requirements_safe(VkImage)const noexcept;
    bool buffer_requirements_safe(VkBuffer)const noexcept;
    // Null requirements retain an unknown-footprint allocation blocker.
    void image_bound(VkImage,VkDeviceMemory,VkDeviceSize,const VkMemoryRequirements*)noexcept;
    void buffer_bound(VkBuffer,VkDeviceMemory,VkDeviceSize,const VkMemoryRequirements*)noexcept;
    void uncertain_memory(VkDeviceMemory)noexcept;
    void disable()noexcept;
    bool exclusive(VkImage,NativeImageBacking&)const noexcept;
    bool current(const NativeImageBacking&)const noexcept;
    bool ordinary_binding(VkImage,NativeImageAllocationBinding&)const noexcept;
    bool current(const NativeImageAllocationBinding&)const noexcept;
    NativeImageMemoryDiagnostic diagnostic(VkImage)const noexcept;
private:
    struct Memory {
        VkDeviceSize size=0;std::uint64_t generation=0,dedicated_generation=0;
        std::uint32_t type=0;VkMemoryPropertyFlags properties=0;
        VkImage dedicated_image{};VkBuffer dedicated_buffer{};
        bool ordinary=false,uncertain=false;
    };
    struct Binding {
        VkDeviceMemory memory{};std::uint64_t memory_generation=0;
        VkDeviceSize offset=0,bytes=0;
        bool attempted=false,known=false;
    };
    struct Object {
        std::uint64_t generation=0,revision=1;
        bool target=false,query_safe=false;
        Binding binding;
    };
    bool next_generation(std::uint64_t&)noexcept;
    bool conflict(const Binding&,const Binding&)const noexcept;
    void changed(const Binding&,VkImage exclude=VK_NULL_HANDLE)noexcept;
    void uncertain_locked(VkDeviceMemory)noexcept;
    void bind(Object&,VkImage,VkBuffer,VkDeviceMemory,VkDeviceSize,const VkMemoryRequirements*)noexcept;
    bool token(VkImage,NativeImageBacking&)const noexcept;
    bool allocation_token(VkImage,NativeImageAllocationBinding&)const noexcept;
    void disable_at(NativeImageDisableReason)const noexcept;
    mutable std::mutex mutex_;
    mutable std::atomic<bool> disabled_{false};
    mutable std::atomic<NativeImageDisableReason> disable_reason_{NativeImageDisableReason::none};
    VkPhysicalDeviceMemoryProperties properties_{};
    VkDeviceSize granularity_=0;
    std::uint64_t generation_=0;
    std::map<VkDeviceMemory,Memory> memories_;
    std::map<VkImage,Object> images_;
    std::map<VkBuffer,Object> buffers_;
#ifdef EVRM_NATIVE_IMAGE_MEMORY_TESTING
    friend struct NativeImageMemoryTestPeer;
#endif
};
}
