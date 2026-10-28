#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace evrm {
// Owned diagnostics only. Caller embeds this POD and changes it only through
// the collector. It certifies no layout, producer, queue, resource or lifetime.
struct NativeTransparencyRecording {
    // dropped counts calls partly/wholly omitted by entry/counter budgets.
    std::uint64_t recording_id=0,command=0,calls=0,dropped=0;
    std::uint32_t entries=0;
    bool selected=false,ended=false,incomplete=false,overflow=false,output_failed=false;
};
// Caller serializes this collector and its recordings (normally backend mutex).
// Output receives an owned line valid for the duration of the callback only.
class NativeTransparencyTrace {
public:
    using Output=std::function<void(const std::string&)>;
    // call_limit applies to combined memory+buffer+image counts before arrays.
    static constexpr std::uint32_t selection_limit=3,entry_limit=256,call_limit=256,line_limit=768;
    NativeTransparencyTrace() noexcept=default;
    NativeTransparencyTrace(const NativeTransparencyTrace&)=delete;
    NativeTransparencyTrace& operator=(const NativeTransparencyTrace&)=delete;
    bool set_output(const Output&) noexcept;
    void begin(NativeTransparencyRecording&,VkCommandBuffer) noexcept;
    void reset(NativeTransparencyRecording&) noexcept;
    bool select(NativeTransparencyRecording&) noexcept;
    void legacy(NativeTransparencyRecording&,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags,
                std::uint32_t memory_count,const VkMemoryBarrier*,std::uint32_t buffer_count,const VkBufferMemoryBarrier*,
                std::uint32_t image_count,const VkImageMemoryBarrier*) noexcept;
    void sync2(NativeTransparencyRecording&,const VkDependencyInfo*) noexcept;
    void end(NativeTransparencyRecording&,VkResult) noexcept;
private:
    Output output_;
    std::uint64_t last_id_=0;
    std::uint32_t selected_count_=0;
    std::array<std::uint64_t,selection_limit> selected_ids_{};
    bool active(const NativeTransparencyRecording&) const noexcept;
    void finish(NativeTransparencyRecording&,VkResult,const char*) noexcept;
#ifdef EVRM_NATIVE_TRANSPARENCY_TRACE_TESTING
    friend struct NativeTransparencyTraceTestPeer;
#endif
};
}
