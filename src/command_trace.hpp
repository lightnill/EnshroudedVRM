#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace evrm {

enum class CommandTraceKind : std::uint32_t {
    command_begin, command_end, pass_begin, next_subpass, pass_end,
    pipeline_bind, dispatch, dispatch_base, dispatch_indirect,
    draw_indexed_indirect, draw_indexed_indirect_count,
    copy_buffer, fill_buffer, update_buffer, execute_secondary, unsupported,
    // Append only: capture readers rely on the numeric IDs above.
    descriptor_sets_bind, rendering_begin, query_begin, query_end,
    conditional_begin, conditional_end
};
enum class CommandTracePass : std::uint32_t {
    legacy, render_pass2, dynamic_rendering, unknown
};
enum CommandTraceIssue : std::uint64_t {
    trace_missing_information = 1ull << 0,
    trace_invalid_state       = 1ull << 1,
    trace_stale_secondary     = 1ull << 2,
    trace_unsupported         = 1ull << 3,
    trace_overflow            = 1ull << 4,
    trace_cycle               = 1ull << 5,
    trace_depth_limit         = 1ull << 6,
    trace_family_mismatch     = 1ull << 7,
    trace_allocation_failure  = 1ull << 8
};

struct CommandTraceRegion {
    std::uint64_t source_offset = 0, destination_offset = 0, size = 0;
};
struct CommandTraceRenderingAttachment {
    std::uint64_t image_view = 0, resolve_image_view = 0;
    std::uint32_t image_layout = 0, resolve_mode = 0, resolve_image_layout = 0;
    std::uint32_t load_op = 0, store_op = 0;
    // Raw VkClearValue representation, preserving float/uint/depth/stencil
    // bits without interpreting ignored or format-dependent members.
    std::array<std::uint32_t, 4> clear_value_bits{};
    std::vector<std::uint32_t> pnext_types;
};
struct CommandTraceRendering {
    VkRect2D render_area{};
    std::uint32_t flags = 0, layer_count = 0, view_mask = 0;
    std::uint32_t color_attachment_count = 0;
    std::vector<CommandTraceRenderingAttachment> colors;
    bool has_depth = false, has_stencil = false;
    CommandTraceRenderingAttachment depth, stencil;
};
struct CommandTraceEvent {
    CommandTraceKind kind = CommandTraceKind::unsupported;
    std::uint64_t command = 0, generation = 0;
    std::uint32_t queue_family = UINT32_MAX;
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    // begin: flags,level,pool,inheritance renderpass/framebuffer/subpass,
    //        queryFlags,pipelineStatistics|(occlusionQueryEnable<<32).
    // pass_begin: kind,renderpass,framebuffer,contents,flags.
    // next_subpass: kind,contents. pass_end: kind.
    // pipeline_bind: point,pipeline. dispatch: x,y,z.
    // dispatch_base: baseX,baseY,baseZ,x,y,z. indirect: buffer,offset.
    // draw: buffer,offset,maxCount,stride,countBuffer,countOffset.
    // copy: source,destination,declaredRegionCount (copied regions below).
    // fill: buffer,offset,size,data. update: buffer,offset,size (no contents).
    // execute: declared secondary count. unsupported: caller's numeric tag.
    // descriptor_sets_bind: point,layout,firstSet,declaredSetCount,
    //                       declaredDynamicOffsetCount (owned arrays below).
    // rendering_begin: flags,declaredColorCount,layerCount,viewMask.
    // query_begin: pool,query,flags. query_end: pool,query.
    // conditional_begin: buffer,offset,flags. conditional_end: no args.
    std::array<std::uint64_t, 8> args{};
    std::vector<CommandTraceRegion> regions;
    // Only numeric sTypes are retained; no pNext/host pointer is stored.
    std::vector<std::uint32_t> pnext_types;
    std::vector<std::uint64_t> descriptor_sets;
    std::vector<std::uint32_t> dynamic_offsets;
    std::optional<CommandTraceRendering> rendering;
};
struct CommandTraceSemaphore {
    std::uint64_t semaphore = 0, stage_mask = 0, value = 0;
    std::uint32_t device_index = 0;
    bool has_stage_mask = false, has_value = false;
    std::vector<std::uint32_t> pnext_types;
};
struct CommandTraceCommandRef {
    std::uint64_t command = 0, generation = 0;
    std::uint32_t device_mask = 0;
    std::vector<std::uint32_t> pnext_types;
};
struct CommandTraceBatch {
    std::uint32_t flags = 0; // VkSubmitInfo2.flags; zero for legacy Submit.
    std::vector<std::uint32_t> pnext_types;
    std::vector<CommandTraceSemaphore> waits, signals;
    std::vector<CommandTraceCommandRef> commands;
    // Flattened execution order, including primary/secondary boundaries.
    std::vector<CommandTraceEvent> events;
};
struct CommandTraceSubmission {
    std::uint64_t serial = 0, queue = 0;
    std::uint32_t queue_family = UINT32_MAX;
    bool submit2 = false;
    std::vector<CommandTraceBatch> batches;
};
struct CommandTraceSnapshot {
    std::uint64_t sequence = 0, issues = 0;
    bool complete = true;
    std::vector<CommandTraceSubmission> submissions;
};

// Pure chronology observation: no Vulkan calls, resource writes, shader
// registries, filesystem operations, or retained application pointers.
// Lifecycle/submission notifications are made AFTER native success. Every
// observation is noexcept; failures make the diagnostic incomplete only.
class CommandTrace {
public:
    static constexpr std::uint32_t max_commands = 512;
    static constexpr std::uint32_t max_events_per_command = 8192;
    static constexpr std::uint32_t max_output_events = 16384;
    static constexpr std::uint32_t max_transfer_regions = 256;
    static constexpr std::uint32_t max_secondary_depth = 16;
    static constexpr std::uint32_t max_submit_batches = 512;
    static constexpr std::uint32_t max_semaphores_per_batch = 512;
    static constexpr std::uint32_t max_pnext_depth = 16;
    static constexpr std::uint32_t max_descriptor_sets = 256;
    static constexpr std::uint32_t max_dynamic_offsets = 1024;
    static constexpr std::uint32_t max_rendering_attachments = 256;
    static constexpr std::uint32_t max_scope_queries = 256;
    // Additional aggregate storage bounds prevent the per-command maxima
    // from multiplying into an unbounded diagnostic allocation.
    static constexpr std::uint32_t max_stored_units = 65536;

    CommandTrace() noexcept;
    ~CommandTrace();
    CommandTrace(const CommandTrace&) = delete;
    CommandTrace& operator=(const CommandTrace&) = delete;

    void queue_known(VkQueue, std::uint32_t family) noexcept;
    void allocated(const VkCommandBufferAllocateInfo&, const VkCommandBuffer*,
                   std::uint32_t family) noexcept;
    void begin(VkCommandBuffer, const VkCommandBufferBeginInfo&) noexcept;
    void end(VkCommandBuffer) noexcept;
    void reset(VkCommandBuffer) noexcept;
    void freed(VkCommandPool, std::uint32_t, const VkCommandBuffer*) noexcept;
    void pool_reset(VkCommandPool) noexcept;
    void pool_destroyed(VkCommandPool) noexcept;

    void pass_begin(VkCommandBuffer, VkRenderPass, VkFramebuffer,
                    VkSubpassContents, CommandTracePass = CommandTracePass::legacy,
                    std::uint32_t flags = 0) noexcept;
    void next_subpass(VkCommandBuffer, VkSubpassContents,
                      CommandTracePass = CommandTracePass::legacy) noexcept;
    void pass_end(VkCommandBuffer,
                  CommandTracePass = CommandTracePass::legacy) noexcept;
    void bind_descriptor_sets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
                              std::uint32_t first_set, std::uint32_t set_count,
                              const VkDescriptorSet*, std::uint32_t dynamic_offset_count,
                              const std::uint32_t* dynamic_offsets) noexcept;
    // Attachment fields are declared immediate values, not proof of their
    // effective use, layout, lifetime, ownership, or a safe insertion point.
    void begin_rendering(VkCommandBuffer, const VkRenderingInfo&) noexcept;
    void query_begin(VkCommandBuffer, VkQueryPool, std::uint32_t query,
                     VkQueryControlFlags) noexcept;
    void query_end(VkCommandBuffer, VkQueryPool, std::uint32_t query) noexcept;
    void conditional_begin(VkCommandBuffer, const VkConditionalRenderingBeginInfoEXT&) noexcept;
    void conditional_end(VkCommandBuffer) noexcept;
    void pipeline_bound(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) noexcept;
    void dispatch(VkCommandBuffer, std::uint32_t, std::uint32_t, std::uint32_t) noexcept;
    void dispatch_base(VkCommandBuffer, std::uint32_t, std::uint32_t, std::uint32_t,
                       std::uint32_t, std::uint32_t, std::uint32_t) noexcept;
    void dispatch_indirect(VkCommandBuffer, VkBuffer, VkDeviceSize) noexcept;
    void draw_indexed_indirect(VkCommandBuffer, VkBuffer, VkDeviceSize,
                               std::uint32_t count, std::uint32_t stride,
                               VkBuffer count_buffer = VK_NULL_HANDLE,
                               VkDeviceSize count_offset = 0) noexcept;
    void copy_buffer(VkCommandBuffer, VkBuffer source, VkBuffer destination,
                     std::uint32_t, const VkBufferCopy*) noexcept;
    void fill_buffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize,
                     std::uint32_t data) noexcept;
    void update_buffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize) noexcept;
    void execute(VkCommandBuffer, std::uint32_t, const VkCommandBuffer*) noexcept;
    void unsupported(VkCommandBuffer, std::uint64_t tag = 0) noexcept;

    void submitted(VkQueue, std::uint32_t, const VkSubmitInfo*) noexcept;
    void submitted2(VkQueue, std::uint32_t, const VkSubmitInfo2*) noexcept;
    // Ends only diagnostic output. Recorded buffers remain available for
    // legitimate reuse on a later submit. Previously returned snapshots own
    // their metadata and are unaffected by subsequent reset/free/rerecord.
    CommandTraceSnapshot finish_frame() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace evrm
