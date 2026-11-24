#pragma once
#include "mod_menu.hpp"
#include "mod_input_win32.hpp"
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <memory>
#include <vector>
namespace evrm {
enum class ModMenuGpuReadiness {Ready, Busy, Failed};
// fence: the submission's own fence, valid when submitted.
struct ModMenuGpuResult {VkResult result=VK_SUCCESS;VkSemaphore completion{};VkFence fence{};bool submitted=false,changed=false;AvatarMenuAction avatar_action;DebugReportAction report_action;};
class ModMenuGpu {
public:
    // signal_completion=false: for devices without present fences. A frame then
    // signals no semaphore and completion() is null; the caller finishes each
    // frame on its fence before presenting.
    ModMenuGpu(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,
        VkQueue,std::uint32_t family,VkFormat,VkExtent2D,const std::vector<VkImage>&,VkImageLayout,float dpi,bool sampled_compute=false,bool signal_completion=true);
    ~ModMenuGpu();
    // wait_ns: how long an open menu may hold this present for the next render
    // slot's earlier frame. Zero only asks. A wait that runs out is still Busy.
    ModMenuGpuReadiness readiness(std::uint64_t wait_ns=0)noexcept;
    bool available()noexcept {return readiness()==ModMenuGpuReadiness::Ready;}
    ModMenuGpuResult draw(std::uint32_t image,std::uint32_t waits,const VkSemaphore*,ModSettings&,const ModMenuStatus&,bool& expanded,bool& close,const ModInputFrame&,ModMenuPlacement* placement=nullptr)noexcept;
    VkSemaphore completion(std::uint32_t)const noexcept;
    void presentation_complete()noexcept;
    void poll_report(const DebugReportStatus&)noexcept;
    void report_presented(std::uint32_t,VkResult)noexcept;
    void report_invalidated()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
