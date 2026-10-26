#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <cstdint>
namespace evrm {
// No submission is rewritten or split. A single known timeline payload is
// forwarded intact; every original wait must also cover private preparation.
inline bool native_legacy_submit_supported(const VkSubmitInfo& s) noexcept {
    if(s.sType!=VK_STRUCTURE_TYPE_SUBMIT_INFO || s.commandBufferCount>512 ||
       (s.commandBufferCount && !s.pCommandBuffers) || s.waitSemaphoreCount>4096 || s.signalSemaphoreCount>4096 ||
       (s.waitSemaphoreCount && (!s.pWaitSemaphores || !s.pWaitDstStageMask)) || (s.signalSemaphoreCount && !s.pSignalSemaphores))return false;
    for(std::uint32_t i=0;i<s.waitSemaphoreCount;++i)
        if(!(s.pWaitDstStageMask[i]&(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)))return false;
    if(!s.pNext)return true;
    const auto& t=*static_cast<const VkTimelineSemaphoreSubmitInfo*>(s.pNext);
    return t.sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO && !t.pNext &&
        t.waitSemaphoreValueCount==s.waitSemaphoreCount && t.signalSemaphoreValueCount==s.signalSemaphoreCount &&
        (!t.waitSemaphoreValueCount || t.pWaitSemaphoreValues) && (!t.signalSemaphoreValueCount || t.pSignalSemaphoreValues);
}
}
