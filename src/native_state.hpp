#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "native_oit_window.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace evrm {
// Current owned dynamic scalars only; no pipeline, readiness or permit authority.
struct NativeRasterState {
    VkViewport viewport{};
    VkRect2D scissor{};
    bool viewport_known=false,scissor_known=false;
    bool matches(VkExtent2D) const noexcept;
};
bool native_state_command_unobserved(const char*) noexcept;
enum class NativePipelineKind { other, visibility, gbuffer, lighting };
struct NativePipelineState {
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    NativePipelineKind kind=NativePipelineKind::other;
    bool supported=false;
    std::uint32_t set_count=0;
    std::uint64_t generation=0;
    std::uint32_t vertex_bytes=0;
    std::array<std::uint8_t,32> vertex_digest{};
    std::uint32_t fragment_bytes=0;
    std::array<std::uint8_t,32> fragment_digest{};
    // Complete observed identity. Tessellation/mesh do not establish a strict
    // first avatar source witness; they are classified separately for coverage.
    std::uint32_t graphics_stage_count=0;
    std::uint32_t tess_control_bytes=0;
    std::array<std::uint8_t,32> tess_control_digest{};
    std::uint32_t tess_evaluation_bytes=0;
    std::array<std::uint8_t,32> tess_evaluation_digest{};
    // Observational only; strict avatar eligibility is unchanged.
    bool oit_compatible=false;
    bool shadow_compatible=false; // fixed-state observation only; never a first avatar witness
    std::uint32_t compute_bytes=0;
    std::array<std::uint8_t,32> compute_digest{};
    std::uint32_t mesh_bytes=0;
    std::array<std::uint8_t,32> mesh_digest{};
};
struct NativeBoundState {
    NativePipelineState pipeline;
    std::vector<VkDescriptorSet> sets;
    std::vector<std::uint64_t> set_generations,set_versions;
    bool valid=false;
};
struct NativeBufferBinding {
    VkDescriptorBufferInfo info{};
    VkDeviceSize size=0;
    VkBufferUsageFlags usage=0;
    std::uint64_t generation=0,set_generation=0,set_version=0;
    VkDescriptorSet set{};
    std::uint32_t binding=0;
    VkDescriptorBindingFlags declaration_flags=0;
    explicit operator bool() const noexcept;
};
struct NativePassState {
    NativePipelineKind kind=NativePipelineKind::other;
    VkExtent2D extent{};
    VkImageView depth{};
    bool supported=false;
};
// Sampled-resource metadata only: no backing, actual layout, readiness or lease.
struct NativeSampledImageView {
    VkImageView view{};
    VkImage image{};
    VkImageType image_type=VK_IMAGE_TYPE_MAX_ENUM;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageSubresourceRange subresource{};
    VkImageUsageFlags usage=0;
    std::uint64_t image_generation=0,view_generation=0;
    VkImageCreateFlags image_flags=0;
    VkImageUsageFlags image_usage=0;
    VkFormat image_format=VK_FORMAT_UNDEFINED;
    std::uint32_t mip_levels=0,array_layers=0;
    std::uint32_t image_format_count=0;
    std::array<VkFormat,16> image_formats{};
};
struct NativeSampledImageBinding {
    VkDescriptorImageInfo info{};
    NativeSampledImageView view;
    VkDescriptorSet set{};
    std::uint32_t binding=0;
    std::uint64_t set_generation=0,set_version=0;
    VkDescriptorBindingFlags declaration_flags=0;
    explicit operator bool() const noexcept;
};
// Scalar STORAGE_IMAGE metadata only; no backing/use/readiness/lease proof.
struct NativeStorageImageBinding {
    VkDescriptorImageInfo info{};
    NativeSampledImageView view;
    VkDescriptorSet set{};
    std::uint32_t binding=0;
    std::uint64_t set_generation=0,set_version=0;
    VkDescriptorBindingFlags declaration_flags=0;
    explicit operator bool() const noexcept;
};

// Bounded metadata only. No Vulkan calls, resource leases, GPU readiness,
// entity ownership or queue synchronization are established by this registry.
class NativeState {
public:
    NativeState();
    ~NativeState();
    NativeState(const NativeState&)=delete;
    NativeState& operator=(const NativeState&)=delete;
    void buffer_created(VkBuffer,const VkBufferCreateInfo&) noexcept;
    void buffer_destroyed(VkBuffer) noexcept;
    NativeBufferBinding buffer(VkBuffer) const noexcept;
    std::uint64_t buffer_epoch() const noexcept;
    void layout_created(VkDescriptorSetLayout,const VkDescriptorSetLayoutCreateInfo&) noexcept;
    void layout_destroyed(VkDescriptorSetLayout) noexcept;
    void pipeline_layout_created(VkPipelineLayout,const VkPipelineLayoutCreateInfo&) noexcept;
    void pipeline_layout_destroyed(VkPipelineLayout) noexcept;
    void sets_allocated(const VkDescriptorSetAllocateInfo&,const VkDescriptorSet*) noexcept;
    void sets_freed(std::uint32_t,const VkDescriptorSet*) noexcept;
    void pool_reset(VkDescriptorPool) noexcept;
    void descriptors_written(std::uint32_t,const VkWriteDescriptorSet*) noexcept;
    void descriptors_copied(std::uint32_t,const VkCopyDescriptorSet*) noexcept;
    void template_created(VkDescriptorUpdateTemplate,const VkDescriptorUpdateTemplateCreateInfo&) noexcept;
    void template_destroyed(VkDescriptorUpdateTemplate) noexcept;
    void template_written(VkDescriptorSet,VkDescriptorUpdateTemplate,const void*) noexcept;
    void shader_created(VkShaderModule,const VkShaderModuleCreateInfo&) noexcept;
    void shader_destroyed(VkShaderModule) noexcept;
    void graphics_pipelines_created(std::uint32_t,const VkGraphicsPipelineCreateInfo*,const VkPipeline*) noexcept;
    void compute_pipelines_created(std::uint32_t,const VkComputePipelineCreateInfo*,const VkPipeline*) noexcept;
    void pipeline_destroyed(VkPipeline) noexcept;
    void image_created(VkImage,const VkImageCreateInfo&) noexcept;
    void image_destroyed(VkImage) noexcept;
    void view_created(VkImageView,const VkImageViewCreateInfo&) noexcept;
    void view_destroyed(VkImageView) noexcept;
    void command_begin(VkCommandBuffer) noexcept;
    void command_freed(std::uint32_t,const VkCommandBuffer*) noexcept;
    void viewports_set(VkCommandBuffer,std::uint32_t first,std::uint32_t count,const VkViewport*) noexcept;
    void scissors_set(VkCommandBuffer,std::uint32_t first,std::uint32_t count,const VkRect2D*) noexcept;
    void raster_unobserved(VkCommandBuffer,const char* exact_name) noexcept;
    NativeRasterState raster(VkCommandBuffer) const noexcept;
    void pipeline_bound(VkCommandBuffer,VkPipelineBindPoint,VkPipeline) noexcept;
    void sets_bound(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,std::uint32_t,std::uint32_t,
                    const VkDescriptorSet*,std::uint32_t,const std::uint32_t*) noexcept;
    NativeBoundState bound(VkCommandBuffer,VkPipelineBindPoint) const noexcept;
    // Complete, current binding metadata even for a pipeline outside the
    // avatar rendering contract. This grants no body/ownership exemption.
    NativeBoundState observed_bound(VkCommandBuffer,VkPipelineBindPoint) const noexcept;
    std::string binding_diagnostic(VkCommandBuffer,VkPipelineBindPoint) const noexcept;
    NativeBufferBinding buffer_binding(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    NativeBufferBinding observed_buffer_binding(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    std::string buffer_binding_diagnostic(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    bool live(const NativeBufferBinding&) const noexcept;
    // Current legacy attachment metadata only; existing2D admission is unchanged.
    NativeOitView attachment_view(VkImageView) const noexcept;
    // Bounded retained scalar sampled/storage payload facts, never a source certificate.
    std::string sampled_binding_diagnostic(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    NativeSampledImageView sampled_view(VkImageView) const noexcept;
    NativeSampledImageBinding observed_sampled_image_binding(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    bool live(const NativeSampledImageBinding&) const noexcept;
    NativeStorageImageBinding observed_storage_image_binding(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    bool live(const NativeStorageImageBinding&) const noexcept;
    NativePassState rendering(const VkRenderingInfo&) const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace evrm
