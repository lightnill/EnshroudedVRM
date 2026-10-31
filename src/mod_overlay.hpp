#pragma once
#include "graphics_state.hpp"
#include "mod_menu_status.hpp"
#include <vulkan/vk_layer.h>
#include <windows.h>
#include <memory>
namespace evrm {
bool mod_menu_device_supported(const VkDeviceCreateInfo&)noexcept;
struct ModOverlayPresentation {
    mutable VkPresentInfoKHR present{};
    VkSemaphore wait{};VkFence present_fence{};
    mutable VkSwapchainPresentFenceInfoKHR fence_info{};
    VkResult stop=VK_SUCCESS;
    bool inserted=false,private_fence=false,consumed=false;
    // Construct references only on the final caller-owned object, after moves.
    const VkPresentInfoKHR* view()const noexcept;
};
class ModOverlay {
public:
    ModOverlay(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,bool enabled,VkImageLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ~ModOverlay();
    void swapchain_created(const SwapchainState&,HWND,bool eligible)noexcept;
    void swapchain_destroyed(VkSwapchainKHR)noexcept;
    void acquired(VkSwapchainKHR,std::uint32_t,VkResult)noexcept;
    bool retries_present(const VkPresentInfoKHR&)noexcept;
    void before_present(VkQueue,std::uint32_t family,const VkPresentInfoKHR&,bool consumed,bool accessible,const ModMenuStatus&,ModOverlayPresentation&)noexcept;
    void presented(const ModOverlayPresentation&,VkResult)noexcept;
    void fence_changing(VkFence,bool destroying)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
