#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace evrm {
struct GpuBufferSnapshot {
    VkDescriptorBufferInfo binding{};
    std::vector<std::uint8_t> bytes;
};
struct CulledInstanceSpan {
    uint32_t first_instance=0,instance_count=0;
};
struct PlayerAnchorDispatch {
    uint32_t x=0,y=0,z=0;
};
struct GpuSnapshot {
    std::map<std::string, GpuBufferSnapshot> buffers;
    std::vector<CulledInstanceSpan> culled_spans;
    uint32_t queue_family = 0;
    uint64_t source_epoch=0,captured_milliseconds=0,sequence=0;
    bool asynchronous=false;
    // Independent instance allocations sampled at the same proven completion.
    // Never merge their culled records, cameras or skin poses.
    std::vector<GpuSnapshot> additional_views;
    // Optional diagnostic from exact-classified submitted compute work. A
    // missing role is never evidence of ownership and never invalidates pose.
    uint32_t player_anchor_candidate_count = 0;
    std::vector<PlayerAnchorDispatch> player_anchor_dispatches;
};
// Development capture of the exact buffers bound by the game. No draw suppression.
class GpuProbe {
public:
    GpuProbe(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
             PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa,
             PFN_vkSetDeviceLoaderData set_loader_data);
    ~GpuProbe();
    bool enabled() const;
    bool needs_transfer_source(const VkBufferCreateInfo&) const;
    void buffer_created(VkBuffer, const VkBufferCreateInfo&);
    void buffer_destroyed(VkBuffer);
    void buffer_bound(VkBuffer,VkDeviceMemory,bool ordinary);
    void memory_destroyed(VkDeviceMemory);
    void layout_created(VkDescriptorSetLayout, const VkDescriptorSetLayoutCreateInfo&);
    void layout_destroyed(VkDescriptorSetLayout);
    void sets_allocated(const VkDescriptorSetAllocateInfo&, const VkDescriptorSet*);
    void sets_freed(uint32_t count, const VkDescriptorSet*);
    void pool_reset(VkDescriptorPool);
    void descriptors_written(uint32_t count, const VkWriteDescriptorSet*);
    void descriptors_copied(uint32_t count, const VkCopyDescriptorSet*);
    void template_created(VkDescriptorUpdateTemplate, const VkDescriptorUpdateTemplateCreateInfo&);
    void template_destroyed(VkDescriptorUpdateTemplate);
    void template_written(VkDescriptorSet, VkDescriptorUpdateTemplate, const void*);
    void sets_bound(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,uint32_t first_set,uint32_t count, const VkDescriptorSet*, uint32_t dynamic_count, const uint32_t*);
    void command_begin(VkCommandBuffer);
    void command_freed(uint32_t,const VkCommandBuffer*);
    void commands_allocated(const VkCommandBufferAllocateInfo&,const VkCommandBuffer*);
    void command_pool_reset(VkCommandPool,bool destroyed=false);
    // Caller proves an ordinary one-time primary outside all rendering scopes.
    bool capture_inline(VkCommandBuffer,std::uint32_t family) noexcept;
    void inline_submitted2(VkQueue,uint32_t,const VkSubmitInfo2*,VkResult,std::uint64_t epoch) noexcept;
    void pipeline_bound(VkCommandBuffer,bool model_pipeline,VkPipelineLayout expected_layout=VK_NULL_HANDLE);
    void compute_pipeline_bound(VkCommandBuffer,bool known_pipeline,VkPipelineLayout expected_layout);
    void compute_dispatched(VkCommandBuffer,uint32_t x,uint32_t y,uint32_t z);
    void compute_dispatch_unsupported(VkCommandBuffer);
    void draw_indexed_indirect(VkCommandBuffer,VkBuffer,VkDeviceSize,uint32_t count,uint32_t stride,
                               VkBuffer count_buffer=VK_NULL_HANDLE,VkDeviceSize count_offset=0);
    void execute(VkCommandBuffer,uint32_t,const VkCommandBuffer*);
    void submitted(VkQueue,uint32_t,const VkSubmitInfo*);
    void submitted2(VkQueue,uint32_t,const VkSubmitInfo2*);
    void finish_frame();
    void queue_observed(VkQueue, uint32_t family);
    // Called with the queue's existing external-synchronization lock held.
    void configure_async(VkQueue,const std::map<std::string,std::size_t>&) noexcept;
    VkResult submit_async(VkQueue,uint32_t,const VkSubmitInfo*,VkFence,PFN_vkQueueSubmit,uint64_t source_epoch) noexcept;
    bool async_frame_scheduled(VkQueue) const noexcept;
    GpuSnapshot poll_async(VkQueue) noexcept;
    void capture(VkQueue, uint64_t frame, bool already_idle, uint32_t wait_count = 0,
                 const VkSemaphore* waits = nullptr, bool* consumed_waits = nullptr) noexcept;
    bool player_anchor_capture_enabled() const;
    void capture_snapshot(const GpuSnapshot&,uint64_t frame) noexcept;
    GpuSnapshot readback(VkQueue, const std::map<std::string, std::size_t>& limits,
                         bool already_idle = false, uint32_t wait_count = 0,
                         const VkSemaphore* waits = nullptr, bool* consumed_waits = nullptr) noexcept;
    bool queue_family(VkQueue, uint32_t& family) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
