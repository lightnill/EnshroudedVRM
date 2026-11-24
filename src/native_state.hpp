#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "native_oit_window.hpp"
#include "native_scene_image_metadata.hpp"
#include "native_sampler_metadata.hpp"
#define EVRM_NATIVE_SAMPLER_DESCRIPTORS 1
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

#define EVRM_NATIVE_COMPUTE_ROOT_METADATA 1
#define EVRM_NATIVE_SCENE_IMAGE_METADATA 1

namespace evrm {
// One observed word for an exact compute-only, offset-0/size-4 root layout.
// Metadata only: callers still prove the bound pipeline, producer, lifetime,
// synchronization and scene ownership. A default value has never been pushed.
struct NativeComputeRootValue {
    VkPipelineLayout layout{};
    std::uint64_t layout_generation=0;
    std::uint32_t value=0;
    bool valid=false;
};
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
// Owned declaration facts only. Sampler objects/immutable-sampler state,
// descriptor contents, resource use, execution and source leases are not described.
struct NativeDescriptorDeclaration {
    std::uint32_t binding=0;
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t count=0;
    VkShaderStageFlags stages=0;
    VkDescriptorBindingFlags flags=0;
};
struct NativeDescriptorSetLayoutObservation {
    VkDescriptorSetLayout layout{};
    std::uint64_t generation=0;
    VkDescriptorSet set{};
    std::uint64_t set_generation=0,set_version=0;
    std::vector<NativeDescriptorDeclaration> bindings;
    explicit operator bool()const noexcept{return layout&&generation&&set&&set_generation&&set_version;}
};
#define EVRM_NATIVE_LAYOUT_DECLARATIONS 1
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
// Last forwarded dispatch domain only. Indirect GPU payload/group counts,
// producer execution, resource backing/lease and scene readiness are unproven.
enum class NativeDispatchKind { Unknown,Direct,Indirect };
struct NativeDispatchObservation {
    NativeDispatchKind kind=NativeDispatchKind::Unknown;
    std::uint64_t serial=0;
    std::array<std::uint32_t,3> groups{};
    NativeBufferBinding indirect_buffer;
    VkDeviceSize indirect_offset=0;
    explicit operator bool()const noexcept{
        return serial&&((kind==NativeDispatchKind::Direct&&!indirect_buffer&&!indirect_offset)||
            (kind==NativeDispatchKind::Indirect&&bool(indirect_buffer)&&groups==std::array<std::uint32_t,3>{}));
    }
};
#define EVRM_NATIVE_DISPATCH_METADATA 1
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
// Exact observed scalar descriptor value and object-at-write identity only.
// No backing, initialization, actual layout, source lease or scene authority.
struct NativeSceneImageDescriptor {
    VkDescriptorImageInfo info{};
    NativeSceneImageToken image;
    VkDescriptorSet set{};
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::uint32_t binding=0;
    std::uint64_t set_generation=0,set_version=0;
    VkDescriptorBindingFlags declaration_flags=0;
    explicit operator bool()const noexcept{
        return set&&set_generation&&set_version&&bool(image)&&info.imageView==image.view&&!info.sampler&&
            (type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE||type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }
};
#define EVRM_NATIVE_SCENE_IMAGE_DESCRIPTORS 1
#define EVRM_NATIVE_PIPELINE_OBSERVATION 1
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
    void sampler_created(VkSampler,const VkSamplerCreateInfo&) noexcept;
    void sampler_destroyed(VkSampler) noexcept;
    NativeSamplerDescriptor observed_sampler_descriptor(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    bool sampler_descriptor_current(const NativeSamplerDescriptor&) const noexcept;
    bool sampler_metadata_available() const noexcept;
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
    // Full mip-zero upload/dependency evidence only. Caller additionally proves
    // original Deferred use, backing, lifetime, ownership and current descriptors.
    void cube_copy(VkCommandBuffer,VkImage,VkImageLayout,std::uint32_t,const VkBufferImageCopy*) noexcept;
    void cube_copy2(VkCommandBuffer,const VkCopyBufferToImageInfo2*) noexcept;
    void cube_unobserved_write(VkImage) noexcept;
    void cube_barriers(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,std::uint32_t,const VkImageMemoryBarrier*) noexcept;
    void cube_barriers2(VkCommandBuffer,const VkDependencyInfo*) noexcept;
    void cube_command_end(VkCommandBuffer,VkResult) noexcept;
    void cube_reset_commands() noexcept;
    void cube_allocated(const VkCommandBufferAllocateInfo&,const VkCommandBuffer*) noexcept;
    void cube_freed(std::uint32_t,const VkCommandBuffer*) noexcept;
    void cube_pool_reset(VkCommandPool,bool destroyed) noexcept;
    void cube_submitted(VkQueue,std::uint32_t,const VkSubmitInfo*,VkResult,VkFence=VK_NULL_HANDLE) noexcept;
    void cube_queue_observed(VkQueue,std::uint32_t) noexcept;
    void cube_fence_changing(std::uint32_t,const VkFence*) noexcept;
    void cube_fence_created(VkFence,const VkFenceCreateInfo*) noexcept;
    void cube_fence_retired(VkFence) noexcept;
    void cube_fence_result(std::uint32_t,const VkFence*,VkBool32 wait_all,VkResult,bool status_query=false) noexcept;
    void cube_queue_idle(VkQueue,VkResult) noexcept;
    void cube_before_submit(std::uint32_t,const VkSubmitInfo*) noexcept;
    void cube_executed(std::uint32_t,const VkCommandBuffer*) noexcept;
    void cube_submitted2(std::uint32_t,const VkSubmitInfo2*) noexcept;
    bool cube_initialized(const NativeSceneImageToken&,VkQueue) const noexcept;
    std::string cube_diagnostic(const NativeSceneImageToken&,VkQueue) const noexcept;
    void dispatch_observed(VkCommandBuffer,bool direct,std::uint32_t x=0,std::uint32_t y=0,std::uint32_t z=0,
                           VkBuffer indirect_buffer=VK_NULL_HANDLE,VkDeviceSize indirect_offset=0) noexcept;
    NativeDispatchObservation observed_dispatch(VkCommandBuffer) const noexcept;
    void push_constants(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags,
                        std::uint32_t offset,std::uint32_t size,const void*) noexcept;
    void compute_root_unobserved(VkCommandBuffer,const char* exact_name) noexcept;
    NativeComputeRootValue observed_compute_root(VkCommandBuffer,VkPipelineLayout) const noexcept;
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
    // Last observed classic pipeline binding with matching catalog lifetimes.
    // Independent of complete sets; grants no execution or scene authority.
    NativePipelineState observed_pipeline(VkCommandBuffer,VkPipelineBindPoint) const noexcept;
    NativeDescriptorSetLayoutObservation observed_set_layout(const NativeBoundState&,std::uint32_t) const noexcept;
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
    // Current object facts only, separate from legacy source eligibility.
    // A current handle lookup does not attest descriptor-at-write provenance.
    NativeSceneImageFacts scene_image_view(VkImageView) const noexcept;
    // Token lookup never joins a recycled raw handle to a different object.
    NativeSceneImageFacts scene_image_view(const NativeSceneImageToken&) const noexcept;
    NativeSceneImageDescriptor observed_scene_image_descriptor(const NativeBoundState&,std::uint32_t,std::uint32_t) const noexcept;
    bool scene_image_descriptor_current(const NativeSceneImageDescriptor&) const noexcept;
    bool scene_image_metadata_current(const NativeSceneImageToken&) const noexcept;
    bool scene_image_metadata_available() const noexcept;
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
