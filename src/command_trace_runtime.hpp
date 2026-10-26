#pragma once

#include "command_trace.hpp"
#include <filesystem>
#include <memory>

namespace evrm {

// Optional metadata and fixed-marker writer. This observer makes no Vulkan
// calls and cannot establish ownership, source readiness, or complete coverage.
// Image metadata covers declared color/depth attachments and undecoded pNext
// cases; views require a live retained image at creation. Other images/views
// are deliberately excluded without retaining a cache of ignored handles.
class CommandTraceRuntime {
public:
    using Clock = std::uint64_t (*)() noexcept;
    static constexpr std::uint32_t max_registry_entries = 4096;
    static constexpr std::uint32_t max_array_entries = 256;
    static constexpr std::uint32_t max_name_bytes = 256;
    static constexpr std::uint32_t max_metadata_pnext_depth = 16;
    static constexpr std::uint64_t max_shader_bytes = 8ull * 1024 * 1024;

    // Empty capture_directory disables metadata/writes. Even when disabled,
    // finish_frame always consumes the pure collector's pending output.
    explicit CommandTraceRuntime(const std::filesystem::path& capture_directory = {},
                                 Clock = nullptr) noexcept;
    ~CommandTraceRuntime();
    CommandTraceRuntime(const CommandTraceRuntime&) = delete;
    CommandTraceRuntime& operator=(const CommandTraceRuntime&) = delete;

    CommandTrace& collector() noexcept { return collector_; }
    void buffer_created(VkBuffer, const VkBufferCreateInfo&) noexcept;
    void buffer_destroyed(VkBuffer) noexcept;
    void image_created(VkImage, const VkImageCreateInfo&) noexcept;
    void image_destroyed(VkImage) noexcept;
    void image_view_created(VkImageView, const VkImageViewCreateInfo&) noexcept;
    void image_view_destroyed(VkImageView) noexcept;
    void shader_created(VkShaderModule, const VkShaderModuleCreateInfo&) noexcept;
    void shader_destroyed(VkShaderModule) noexcept;
    void graphics_pipelines_created(std::uint32_t, const VkGraphicsPipelineCreateInfo*,
                                    const VkPipeline*) noexcept;
    void compute_pipelines_created(std::uint32_t, const VkComputePipelineCreateInfo*,
                                   const VkPipeline*) noexcept;
    void pipeline_destroyed(VkPipeline) noexcept;
    void renderpass_created(VkRenderPass, const VkRenderPassCreateInfo&) noexcept;
    void renderpass2_created(VkRenderPass, const VkRenderPassCreateInfo2&) noexcept;
    void renderpass_destroyed(VkRenderPass) noexcept;
    void framebuffer_created(VkFramebuffer, const VkFramebufferCreateInfo&) noexcept;
    void framebuffer_destroyed(VkFramebuffer) noexcept;
    void finish_frame(std::uint64_t frame) noexcept;

private:
    struct Impl;
    CommandTrace collector_;
    std::unique_ptr<Impl> impl_;
};

} // namespace evrm
