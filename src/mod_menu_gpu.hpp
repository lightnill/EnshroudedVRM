#pragma once
#include "mod_menu.hpp"
#include "mod_input_win32.hpp"
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <memory>
#include <vector>
namespace evrm {
struct ModMenuGpuResult {VkResult result=VK_SUCCESS;VkSemaphore completion{};bool submitted=false,changed=false;AvatarMenuAction avatar_action;};
class ModMenuGpu {
public:
    ModMenuGpu(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,
        VkQueue,std::uint32_t family,VkFormat,VkExtent2D,const std::vector<VkImage>&,VkImageLayout,float dpi);
    ~ModMenuGpu();
    bool available()noexcept;
    ModMenuGpuResult draw(std::uint32_t image,std::uint32_t waits,const VkSemaphore*,ModSettings&,const ModMenuStatus&,bool& expanded,bool& close,const ModInputFrame&,ModMenuPlacement* placement=nullptr)noexcept;
    VkSemaphore completion(std::uint32_t)const noexcept;
    void presentation_complete()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
