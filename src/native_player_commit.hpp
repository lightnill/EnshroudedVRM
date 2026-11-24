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

// Retain ALL frame, subject-mask and aggregate-output leases until completion. This
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

// Clear and accumulate ready-only clipping after all subject prepares. Four
// storage bindings fit the core minimum; individual prepared frames are read-only.
class NativePlayerCommit {
public:
    NativePlayerCommit(VkDevice,PFN_vkGetDeviceProcAddr,const std::uint32_t* words,std::size_t word_count);
    ~NativePlayerCommit();
    NativePlayerCommit(const NativePlayerCommit&)=delete;
    NativePlayerCommit& operator=(const NativePlayerCommit&)=delete;
    // 1..16 frame/mask pairs; explicit ranges >=196/16384; outputs >=16384/196.
    // Every handle is distinct. Offsets are 256-byte aligned, ranges are
    // 4-byte aligned and cannot overflow. Caller proves backing bounds, usage
    // and disjoint physical memory leases. No host writes occur in batch().
    std::shared_ptr<NativePlayerCommitBatch> batch(const std::vector<VkDescriptorBufferInfo>& subject_frames,
        const std::vector<VkDescriptorBufferInfo>& subject_masks,VkDescriptorBufferInfo effective_mask,VkDescriptorBufferInfo clip_control);
    // Clear outputs once, compute dependency, atomic accumulation per subject.
    // Caller restores compute state and publishes SHADER_READ and UNIFORM_READ
    // for vertex/fragment/mesh clipping. No queue waits or readbacks.
    bool record(VkCommandBuffer,const NativePlayerCommitBatch&) noexcept;
    void quarantine() noexcept;
private:
    std::shared_ptr<NativePlayerCommitState> impl;
};
}
