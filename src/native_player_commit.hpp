#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace evrm {
struct NativePlayerCommitState;
class NativePlayerCommit;

// Retain with ALL subject frame and gate buffer leases until completion. This
// owns descriptors and a shared pipeline lifetime; it never owns buffer memory.
class NativePlayerCommitBatch {
public:
    ~NativePlayerCommitBatch();
    NativePlayerCommitBatch(const NativePlayerCommitBatch&)=delete;
    NativePlayerCommitBatch& operator=(const NativePlayerCommitBatch&)=delete;
    // Sticky shared uncertainty: never free Vulkan objects or record again.
    void quarantine() noexcept;
private:
    friend class NativePlayerCommit;
    struct Impl;
    explicit NativePlayerCommitBatch(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};

// Two-phase common admission after all per-subject prepare dispatches. Only two
// storage bindings are used, including on devices with core minimum limits.
class NativePlayerCommit {
public:
    NativePlayerCommit(VkDevice,PFN_vkGetDeviceProcAddr,const std::uint32_t* words,std::size_t word_count);
    ~NativePlayerCommit();
    NativePlayerCommit(const NativePlayerCommit&)=delete;
    NativePlayerCommit& operator=(const NativePlayerCommit&)=delete;
    // 1..16 unique frame buffers, explicit ranges >=196 and >=4 respectively.
    // Offsets use portable 256-byte alignment; ranges use 4-byte alignment and
    // cannot overflow. Gate must have a distinct buffer handle. Caller proves
    // actual backing bounds, STORAGE_BUFFER usage and nonaliasing memory leases.
    // Gate is caller-initialized coherent host memory; batch never writes it.
    std::shared_ptr<NativePlayerCommitBatch> batch(const std::vector<VkDescriptorBufferInfo>& subject_frames,VkDescriptorBufferInfo gate);
    // Initial host/compute dependency, all reductions, compute dependency, all
    // propagations. Caller restores native compute state and records the final
    // compute->vertex/fragment/mesh/host dependency. No queue waits or readbacks.
    bool record(VkCommandBuffer,const NativePlayerCommitBatch&) noexcept;
    void quarantine() noexcept;
private:
    std::shared_ptr<NativePlayerCommitState> impl;
};
}
