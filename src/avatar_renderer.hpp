#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <memory>
#include "native_publication.hpp"
#include "mod_menu_status.hpp"
#include "avatar_service.hpp"

namespace evrm {
class GpuProbe;
class GraphicsState;
class AvatarRenderer {
public:
    AvatarRenderer(VkInstance, VkPhysicalDevice, VkDevice,
                   PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
                   PFN_vkSetDeviceLoaderData);
    ~AvatarRenderer();
    bool enabled() const;
    ModMouthSupport mouth_support()const noexcept;
    std::shared_ptr<const NativeAvatarPublication> native_publication() const noexcept;
    std::shared_ptr<const PreparedAvatar> prepared_model()const noexcept;
    bool stage_candidate(const AvatarCandidatePtr&,bool gpu_ready,std::string& error)noexcept;
    bool commit_candidate(const AvatarCandidate&,std::uint64_t source_epoch)noexcept;
    // If the original presentation semaphores have been consumed by our GPU
    // submission, the caller must present with zero waits, including on failure.
    bool before_present(VkQueue, const VkPresentInfoKHR&, GpuProbe&, GraphicsState&,
                        bool& consumed_waits,uint64_t source_buffer_epoch=0) noexcept;
    // Called after metadata retirement and before native resource destruction.
    // Waits for this renderer's synchronous use and releases dependent views.
    void native_image_destroyed(VkImage) noexcept;
    void native_swapchain_destroyed(VkSwapchainKHR) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
