#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace evrm {
struct NativePipelineState;
// Owned, current metadata supplied by the observer. This is not a resource
// lease, backing proof, layout transition or initialization/producer proof.
struct NativeOitView {
    VkImageView view{};
    VkImage image{};
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D;
    VkExtent3D extent{};
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage=0;
    VkImageSubresourceRange subresource{};
    std::uint64_t image_generation=0,view_generation=0;
    bool current=false;
};
enum class NativeOitShader { unsupported, unlit, lit_diffuse, lit_shaded };
enum class NativeVolumeProducer { unsupported, populate, in_scatter, integrate };
// Exact compute identity only; no dispatch, descriptor or lifetime proof.
NativeVolumeProducer native_volume_producer(std::uint32_t,const std::array<std::uint8_t,32>&) noexcept;
NativeOitShader native_oit_late_shader(const NativePipelineState&) noexcept;
bool native_oit_pipeline_contract(const VkGraphicsPipelineCreateInfo&) noexcept;
bool native_oit_final_pass(const VkRenderingInfo&,const std::array<NativeOitView,5>&,
                           const NativeOitView& depth,const NativeOitView& expected_depth,
                           VkExtent2D expected_extent) noexcept;

// One matched final pass and one completed insertion in this recording.
// Resource/current-source/restore-state checks remain the caller's gate.
struct NativeOitWindow {
    std::uint32_t candidates=0,insertions=0;
    bool active=false,lit=false,failed=false;
    void begin(bool matching_pass) noexcept;
    void observe_lit(bool compatible_lit_state) noexcept;
    bool can_insert() const noexcept;
    void end(bool inserted) noexcept;
    bool complete() const noexcept;
    void reset() noexcept;
};
}
