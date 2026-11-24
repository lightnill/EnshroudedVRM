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
    // inserted: the menu's completion semaphore and fence go to Present.
    // drawn: this image carries a finished menu frame and nothing was inserted.
    bool inserted=false,private_fence=false,consumed=false,drawn=false;
    // Construct references only on the final caller-owned object, after moves.
    const VkPresentInfoKHR* view()const noexcept;
};
class ModOverlay {
public:
    // present_fences: what mod_menu_device_supported said of this device. When
    // false, every menu frame is finished on its own draw fence before Present
    // is forwarded, and Present is given no semaphore or fence of the menu's.
    ModOverlay(VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,PFN_vkSetDeviceLoaderData,bool enabled,VkImageLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,bool present_fences=true);
    ~ModOverlay();
    void swapchain_created(const SwapchainState&,HWND,bool eligible)noexcept;
    void swapchain_destroyed(VkSwapchainKHR)noexcept;
    void acquired(VkSwapchainKHR,std::uint32_t,VkResult)noexcept;
    bool retries_present(const VkPresentInfoKHR&)noexcept;
    void before_present(VkQueue,std::uint32_t family,const VkPresentInfoKHR&,bool consumed,bool accessible,const ModMenuStatus&,ModOverlayPresentation&)noexcept;
    void presented(const ModOverlayPresentation&,VkResult)noexcept;
    void fence_changing(VkFence,bool destroying)noexcept;
    bool menu_open()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
