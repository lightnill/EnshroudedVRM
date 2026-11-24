#pragma once
#include "native_mtoon_probe_copy.hpp"
namespace evrm {
class NativeMToonProbeProgram;
// Retained Converge/classifier sets and the actual post-update parameter slice.
// The caller proves current backing and same-frame insertion under the source
// guard. The GPU joins their spatial values; these handles grant no such proof.
struct NativeMToonProbeFrameSource {
    VkDescriptorSetLayout coefficient_layout{};VkDescriptorSet coefficient_set{};
    VkDescriptorSetLayout classifier_layout{};VkDescriptorSet classifier_set{};
    VkDescriptorBufferInfo source_permit{};
    VkDescriptorBufferInfo post_update_parameters{};
    VkDescriptorBufferInfo first_update_parameters{};
};
// Output storage of retired probe frames that used a shared program: the owned
// output buffer with its memory, and the descriptor pool and set. A frame
// offers its storage here only where it previously destroyed it, so idle
// storage has no recorded or submitted consumer. At most idle_limit storages
// are idle; one that stays idle longer than idle_milliseconds is destroyed by
// release_expired(). Storage is reissued only to a frame with the same program.
// Contents are never reused: record() fills the whole buffer with zero first,
// and all four descriptors are written again for every frame. The source
// permit is never retained. Each storage was admitted under
// NativeMToonProbeFrame::memory_byte_limit on its actual allocation.
// Serialized by the caller with frame construction and recording.
class NativeMToonProbeFrameReuse {
public:
    static constexpr std::size_t idle_limit=2;
    static constexpr std::uint64_t idle_milliseconds=2000;
    using Clock=std::uint64_t(*)()noexcept;
    explicit NativeMToonProbeFrameReuse(Clock clock=nullptr);
    ~NativeMToonProbeFrameReuse();
    NativeMToonProbeFrameReuse(const NativeMToonProbeFrameReuse&)=delete;
    NativeMToonProbeFrameReuse& operator=(const NativeMToonProbeFrameReuse&)=delete;
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
    // Frames completed with new storage and with reissued storage.
    std::uint64_t created_storages()const noexcept;
    std::uint64_t reissued_storages()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    friend class NativeMToonProbeFrame;
};
class NativeMToonProbeFrame {
public:
    static constexpr VkDeviceSize memory_byte_limit=VkDeviceSize(4)*1024*1024;
    NativeMToonProbeFrame(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonProbeFrameSource&,
        const NativeMToonProbeCopyCapabilities&,const std::uint32_t*,std::size_t,const bool* parent_quarantine);
    NativeMToonProbeFrame(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonProbeFrameSource&,
        const NativeMToonProbeCopyCapabilities&,std::shared_ptr<NativeMToonProbeProgram>,const bool* parent_quarantine,
        std::shared_ptr<NativeMToonProbeFrameReuse> reuse={});
    ~NativeMToonProbeFrame();
    NativeMToonProbeFrame(const NativeMToonProbeFrame&)=delete;
    NativeMToonProbeFrame& operator=(const NativeMToonProbeFrame&)=delete;
    // One full direct dispatch; no push writes, submissions or waits. Caller
    // retains this and every native source until original command retirement.
    bool record(VkCommandBuffer,VkPipeline original,VkPipelineLayout compatible_restore,
                std::uint32_t restore_set_count,const VkDescriptorSet*)noexcept;
    VkDescriptorBufferInfo output()const noexcept;
    void quarantine()noexcept;
private:
    NativeMToonProbeFrame(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonProbeFrameSource&,
        const NativeMToonProbeCopyCapabilities&,const std::uint32_t*,std::size_t,const bool*,std::shared_ptr<NativeMToonProbeProgram>,
        std::shared_ptr<NativeMToonProbeFrameReuse>);
    struct Impl;std::unique_ptr<Impl> impl;
};
} // namespace evrm
