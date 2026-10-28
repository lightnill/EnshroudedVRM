#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace evrm {
struct SceneDepth {
    VkImage image{};
    VkFormat format{};
    VkExtent3D extent{};
    VkImageUsageFlags usage{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    VkCompareOp compare{VK_COMPARE_OP_GREATER_OR_EQUAL};
    uint64_t generation = 0;
    uint32_t owner_queue_family = VK_QUEUE_FAMILY_IGNORED;
    VkSharingMode sharing_mode{VK_SHARING_MODE_EXCLUSIVE};
    std::vector<uint32_t> queue_families;
    explicit operator bool() const { return image != VK_NULL_HANDLE; }
};
struct SwapchainState {
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    uint64_t generation = 0;
    VkImageUsageFlags usage = 0;
    VkSharingMode sharing_mode{VK_SHARING_MODE_EXCLUSIVE};
    std::vector<uint32_t> queue_families, image_owners;
    std::vector<VkImage> images;
};
// Observe submitted command order, including secondary command buffers. These
// observations do not mutate the application's Vulkan commands or resources.
class GraphicsState {
public:
    GraphicsState(VkDevice, PFN_vkGetDeviceProcAddr);
    ~GraphicsState();
    void image_created(VkImage, const VkImageCreateInfo&);
    void image_destroyed(VkImage);
    void view_created(VkImageView, const VkImageViewCreateInfo&);
    void view_destroyed(VkImageView);
    void swapchain_created(VkSwapchainKHR, const VkSwapchainCreateInfoKHR&);
    void swapchain_destroyed(VkSwapchainKHR);
    void renderpass_created(VkRenderPass, const VkRenderPassCreateInfo&);
    void renderpass2_created(VkRenderPass, const VkRenderPassCreateInfo2&);
    void renderpass_destroyed(VkRenderPass);
    void framebuffer_created(VkFramebuffer, const VkFramebufferCreateInfo&);
    void framebuffer_destroyed(VkFramebuffer);
    void shader_created(VkShaderModule, const VkShaderModuleCreateInfo&);
    void shader_destroyed(VkShaderModule);
    void pipelines_created(uint32_t, const VkGraphicsPipelineCreateInfo*, const VkPipeline*);
    // Copies exact verified compute-module classification for every nonnull
    // pipeline result, including partial-success Vulkan creation arrays.
    void compute_pipelines_created(uint32_t, const VkComputePipelineCreateInfo*, const VkPipeline*);
    void pipeline_destroyed(VkPipeline);
    bool model_pipeline(VkPipeline) const;
    bool player_anchor_pipeline(VkPipeline) const;
    VkPipelineLayout pipeline_layout(VkPipeline) const;
    void command_begin(VkCommandBuffer);
    void command_freed(uint32_t, const VkCommandBuffer*);
    void begin_pass(VkCommandBuffer, const VkRenderPassBeginInfo&);
    void end_pass(VkCommandBuffer);
    void begin_rendering(VkCommandBuffer, const VkRenderingInfo&);
    void end_rendering(VkCommandBuffer);
    void pipeline_bound(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
    void barriers(VkCommandBuffer, uint32_t, const VkImageMemoryBarrier*);
    void barriers2(VkCommandBuffer, const VkDependencyInfo&);
    void execute(VkCommandBuffer, uint32_t, const VkCommandBuffer*);
    void queue_observed(VkQueue, uint32_t family);
    void submitted(VkQueue, uint32_t, const VkSubmitInfo*);
    void submitted2(VkQueue, uint32_t, const VkSubmitInfo2*);
    SceneDepth scene_depth() const;
    SwapchainState swapchain(VkSwapchainKHR) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
