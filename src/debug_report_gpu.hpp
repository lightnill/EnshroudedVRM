#pragma once
#include "debug_report_types.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
namespace evrm {
// Records into an existing menu command buffer. Never submits or waits.
class DebugReportGpu {
public:
    DebugReportGpu(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,
        VkFormat,VkExtent2D,std::vector<VkImage>,VkImageLayout,bool sampled_compute);
    ~DebugReportGpu(); // Caller has already proved all menu slots retired.
    void poll(const DebugReportStatus&)noexcept;
    bool prepare_capture(const DebugReportStatus&)noexcept;
    bool record_capture(VkCommandBuffer,std::uint32_t image)noexcept;
    void prepare_preview(const DebugReportStatus&,bool all_slots_complete)noexcept;
    void record_preview_upload(VkCommandBuffer)noexcept;
    std::uint64_t preview_texture(const DebugReportStatus&)const noexcept;
    void submitted(VkFence,bool success,std::uint32_t image,bool preview_visible)noexcept;
    void presented(std::uint32_t image,VkResult)noexcept;
    void invalidate()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
