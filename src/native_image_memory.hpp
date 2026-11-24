#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <map>
#include <mutex>

namespace evrm {
// Ordinary exclusive buffer backing observed by the same full image/buffer
// census. Host-visible native UBOs are allowed; contents and lifetime are not
// certified. Overlap history cannot be refreshed by removing an alias.
struct NativeBufferBacking {
    VkBuffer buffer{};VkDeviceMemory memory{};
    std::uint64_t buffer_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,buffer_offset=0,bytes=0;
    std::uint32_t memory_type_index=0;
    explicit operator bool()const noexcept{return buffer&&memory&&buffer_generation&&memory_generation&&overlap_revision&&bytes;}
};
// Ordinary allocation metadata only; known buffer aliases may exist. This is
// neither exclusivity nor permission to read a historical/native buffer. The
// GI consumer uses this solely for counter/list roles its snapshot never reads.
struct NativeScopedBufferBinding {
    VkBuffer buffer{};VkDeviceMemory memory{};
    std::uint64_t buffer_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,buffer_offset=0,bytes=0;
    std::uint32_t memory_type_index=0;std::uint64_t registry_serial=0;
    explicit operator bool()const noexcept{return buffer&&memory&&buffer_generation&&memory_generation&&overlap_revision&&bytes&&registry_serial;}
};
#define EVRM_NATIVE_SCOPED_BUFFER_BINDING 1
struct NativeImageBacking {
    VkImage image{};VkDeviceMemory memory{};
    std::uint64_t image_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,bytes=0;
    std::uint32_t memory_type_index=0;
    explicit operator bool()const noexcept{return image&&memory&&image_generation&&memory_generation&&overlap_revision&&bytes;}
};
// Whole ordinary image footprint with no observed competing binding. This is
// independent of the strict3D GI target policy and grants no initialization,
// layout, queue/sharing ownership, source lease or native-write permission.
struct NativeSceneImageBacking {
    VkImage image{};VkDeviceMemory memory{};
    std::uint64_t image_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,bytes=0;
    std::uint32_t memory_type_index=0;
    std::uint64_t registry_serial=0;
    explicit operator bool()const noexcept{return image&&memory&&image_generation&&memory_generation&&overlap_revision&&bytes&&registry_serial;}
};
#define EVRM_NATIVE_SCENE_IMAGE_BACKING 1
// Whole ordinary image footprint. Fully observed competing image bindings
// may exist; unknown image footprints and all buffer conflicts still reject.
// This grants no alias-content consistency, initialization, layout, ownership,
// source lease or write permission. The original-pass consumer must separately
// prove its exact active resources are physically disjoint and retain the
// original valid-use/synchronization/lifetime scope. Never sample another alias.
struct NativeSceneImageBinding {
    VkImage image{};VkDeviceMemory memory{};
    std::uint64_t image_generation=0,memory_generation=0,overlap_revision=0;
    VkDeviceSize offset=0,bytes=0;std::uint32_t memory_type_index=0;
    std::uint64_t registry_serial=0;
    explicit operator bool()const noexcept{return image&&memory&&image_generation&&memory_generation&&overlap_revision&&bytes&&registry_serial;}
};
#define EVRM_NATIVE_SCENE_IMAGE_BINDING 1
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
#define EVRM_NATIVE_IMAGE_REJECTION_DIAGNOSTIC 1
// Read-only diagnostic facts, never an exclusive-backing certificate.
// Alias samples are bounded metadata identities for matching scene roles;
// they are not serialized as native handles and never authorize image use.
struct NativeImageMemoryDiagnostic {
    NativeImageDisableReason disable_reason=NativeImageDisableReason::none;
    bool observed=false,disabled=false,image_known=false,target=false,query_safe=false;
    bool bind_attempted=false,footprint_known=false,allocation_known=false,allocation_ordinary=false,allocation_uncertain=false;
    std::uint32_t allocations=0,images=0,buffers=0,memory_type=0,image_conflicts=0,buffer_conflicts=0;
    VkMemoryPropertyFlags memory_properties=0;
    VkImageCreateFlags creation_flags=0;VkMemoryAllocateFlags allocation_flags=0;
    VkStructureType creation_extension=VK_STRUCTURE_TYPE_MAX_ENUM,allocation_extension=VK_STRUCTURE_TYPE_MAX_ENUM,allocation_last_extension=VK_STRUCTURE_TYPE_MAX_ENUM;
    bool requirements_observed=false;
    std::uint32_t allocation_device_mask=0,requirement_memory_types=0,unknown_image_conflicts=0,unknown_buffer_conflicts=0;
    VkDeviceSize requirement_bytes=0,requirement_alignment=0;
    std::array<VkImage,16> conflicting_images{};std::array<VkBuffer,16> conflicting_buffers{};
    VkDeviceSize granularity=0,offset=0,bytes=0,allocation_bytes=0;
    VkDeviceMemory memory{};
    std::uint64_t bound_memory_generation=0,current_memory_generation=0;
};
enum class NativeBufferBackingRejection : std::uint32_t {
    none,registry_disabled,unknown_buffer,unsafe_creation,unknown_footprint,
    invalid_range,unknown_allocation,stale_allocation,nonordinary_allocation,
    uncertain_allocation,prohibited_memory,image_conflict,buffer_conflict,metadata_exception
};
// Explanation only. Requested-range counts are separate from the current
// whole-object exclusion policy and never authorize a read or relax that policy.
struct NativeBufferBackingDiagnostic {
    NativeBufferBackingRejection reason=NativeBufferBackingRejection::none;
    NativeImageDisableReason disable_reason=NativeImageDisableReason::none;
    bool observed=false,disabled=false,eligible=false,buffer_known=false,query_safe=false,footprint_known=false,range_known=false;
    bool allocation_known=false,allocation_ordinary=false,allocation_uncertain=false;
    std::uint32_t memory_type=0,image_conflicts=0,buffer_conflicts=0,range_image_conflicts=0,range_buffer_conflicts=0;
    std::uint32_t unknown_image_conflicts=0,unknown_buffer_conflicts=0;
    VkMemoryPropertyFlags memory_properties=0;
    VkDeviceSize logical_bytes=0,requested_offset=0,requested_bytes=0,binding_offset=0,footprint_bytes=0,allocation_bytes=0,granularity=0;
    VkDeviceMemory memory{};std::uint64_t buffer_generation=0,bound_memory_generation=0,current_memory_generation=0;
};
// Host metadata only. No driver calls, contents/layout certificate, pending
// lifetime lease or physical native destruction deferral. Caller serializes
// native source construction/retirement with the backend source guard.
#define EVRM_NATIVE_STRICT_ALIAS_CACHE 1
#define EVRM_NATIVE_ALLOCATION_BINDING_INDEX 1
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
    bool scene_image_binding(VkImage,NativeSceneImageBinding&)const noexcept;
    bool current(const NativeSceneImageBinding&)const noexcept;
    bool unaliased_scene_image(VkImage,NativeSceneImageBacking&)const noexcept;
    bool current(const NativeSceneImageBacking&)const noexcept;
    bool exclusive(VkImage,NativeImageBacking&)const noexcept;
    bool current(const NativeImageBacking&)const noexcept;
    bool exclusive_buffer(VkBuffer,VkDeviceSize,VkDeviceSize,NativeBufferBacking&)const noexcept;
    bool scoped_buffer_binding(VkBuffer,VkDeviceSize,VkDeviceSize,NativeScopedBufferBinding&)const noexcept;
    bool current(const NativeScopedBufferBinding&)const noexcept;
    bool current(const NativeBufferBacking&)const noexcept;
    bool ordinary_binding(VkImage,NativeImageAllocationBinding&)const noexcept;
    bool current(const NativeImageAllocationBinding&)const noexcept;
    NativeImageMemoryDiagnostic diagnostic(VkImage)const noexcept;
    NativeBufferBackingDiagnostic diagnostic_buffer(VkBuffer,VkDeviceSize offset,VkDeviceSize bytes)const noexcept;
private:
    struct Binding;
    struct Memory {
        VkDeviceSize size=0;std::uint64_t generation=0,dedicated_generation=0;
        std::uint32_t type=0;VkMemoryPropertyFlags properties=0;
        VkImage dedicated_image{};VkBuffer dedicated_buffer{};
        bool ordinary=false,uncertain=false;
        VkStructureType extension=VK_STRUCTURE_TYPE_MAX_ENUM,last_extension=VK_STRUCTURE_TYPE_MAX_ENUM;
        VkMemoryAllocateFlags flags=0;std::uint32_t device_mask=0;
        // Every attempted binding in this allocation generation, including
        // unknown/unsafe footprints. Map nodes stay stable under mutex_.
        Binding* image_bindings=nullptr;Binding* buffer_bindings=nullptr;
    };
    struct Binding {
        VkDeviceMemory memory{};std::uint64_t memory_generation=0;
        VkDeviceSize offset=0,bytes=0;
        VkDeviceSize requirement_bytes=0,requirement_alignment=0;std::uint32_t requirement_memory_types=0;
        bool attempted=false,known=false,requirements_observed=false;
        Binding* index_next=nullptr;
    };
    struct Object {
        std::uint64_t generation=0,revision=1;
        // Positive alias-census result only, under mutex_ and this overlap
        // revision. Allocation/generation/uncertainty guards still run each call.
        mutable std::uint64_t scene_binding_revision=0;
        // Separate strict whole-object alias proof. The owning registry/map/
        // object lifetime and immutable binding scope this memo; all current
        // eligibility/allocation/range checks still precede every cache hit.
        mutable std::uint64_t strict_alias_revision=0;
        // Separate metadata policy; never warms or promotes strict exclusivity.
        mutable std::uint64_t scoped_buffer_revision=0;
        VkDeviceSize logical_bytes=0;
        bool target=false,query_safe=false;
        VkImageCreateFlags creation_flags=0;VkStructureType creation_extension=VK_STRUCTURE_TYPE_MAX_ENUM;
        Binding binding;
    };
    bool next_generation(std::uint64_t&)noexcept;
    bool conflict(const Binding&,const Binding&)const noexcept;
    bool buffer_conflict(const Binding&,const Binding&)const noexcept;
    void changed(const Binding&,VkImage exclude=VK_NULL_HANDLE,VkBuffer exclude_buffer=VK_NULL_HANDLE)noexcept;
    void uncertain_locked(VkDeviceMemory)noexcept;
    void bind(Object&,VkImage,VkBuffer,VkDeviceMemory,VkDeviceSize,const VkMemoryRequirements*)noexcept;
    void unindex(Binding&,bool image)noexcept;
    static std::uint64_t new_registry_serial()noexcept;
    bool scene_binding_token(VkImage,NativeSceneImageBinding&)const noexcept;
    bool scene_token(VkImage,NativeSceneImageBacking&)const noexcept;
    bool token(VkImage,NativeImageBacking&)const noexcept;
    bool buffer_token(VkBuffer,VkDeviceSize,VkDeviceSize,NativeBufferBacking&)const noexcept;
    bool scoped_buffer_token(VkBuffer,VkDeviceSize,VkDeviceSize,NativeScopedBufferBinding&)const noexcept;
    bool allocation_token(VkImage,NativeImageAllocationBinding&)const noexcept;
    void disable_at(NativeImageDisableReason)const noexcept;
    mutable std::mutex mutex_;
    mutable std::atomic<bool> disabled_{false};
    mutable std::atomic<NativeImageDisableReason> disable_reason_{NativeImageDisableReason::none};
    VkPhysicalDeviceMemoryProperties properties_{};
    VkDeviceSize granularity_=0;
    const std::uint64_t registry_serial_=new_registry_serial();
    std::uint64_t generation_=0;
    std::map<VkDeviceMemory,Memory> memories_;
    std::map<VkImage,Object> images_;
    std::map<VkBuffer,Object> buffers_;
#ifdef EVRM_NATIVE_IMAGE_MEMORY_TESTING
    friend struct NativeImageMemoryTestPeer;
#endif
};
}
