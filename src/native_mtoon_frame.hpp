#pragma once
#include "native_mtoon_raster_program.hpp"
#include "native_mtoon_work.hpp"
#include "native_mtoon_shade.hpp"
#include "native_mtoon_outline_control.hpp"
#include "native_mtoon_forward.hpp"
#include "native_mtoon_profile.hpp"
#include "native_mtoon_numeric.hpp"
#include "mtoon_gpu_material.hpp"
namespace evrm {
struct NativeMToonFrameGeometry {
    VkDescriptorBufferInfo frame{},material{},vertices{},indices{},surface{},enhanced_uniform{};
    VkDescriptorImageInfo base{};std::array<VkDescriptorImageInfo,3> enhanced_textures{};
};
struct NativeMToonFrameMaterial {
    VkDescriptorBufferInfo uniform{};std::array<VkDescriptorImageInfo,11> textures{};
};
struct NativeMToonFrameHistory {
    VkDescriptorBufferInfo raw_uv{},frame{},vertices{},legacy{},previous_legacy{},batches{},previous_batches{};
};
struct NativeMToonFrameDraw {
    // Retains the exact uploaded leaves, current/previous geometry, prepared
    // frame and appearance/UV provenance. Never a native scene/source lease.
    std::shared_ptr<const void> lease;
    NativeMToonRasterUniform uniform;NativeMToonOutlineControl control;
    NativeMToonFrameGeometry geometry;NativeMToonFrameMaterial material;NativeMToonFrameHistory history;
    bool selected=false,outline=false,enhanced=false,needs_surface=false,legacy_required=false;
    std::uint32_t forward_actor=0;bool capture_only=false;VkDescriptorBufferInfo forward_surface{};
    // Only the already-converted frontend cutout path supplies this immutable
    // request-plan value. The constructor copies it into its owned control bank;
    // the source upload/world alpha mode is never changed. lease owns the plan.
    const MToonGpuMaterial* menu_cutout_material=nullptr;
    std::uint32_t shadow_actor=0; // Current rendering actor, never a diagnostic label.
    VkCullModeFlags cull=VK_CULL_MODE_NONE,outline_cull=VK_CULL_MODE_FRONT_BIT;
    std::uint32_t numeric_material=0,numeric_actor=0; // Private observation labels only.
};
// Certify only a contiguous opaque-emitting main-rank interval. A missing or
// interleaved owner cannot authorize another actor's/background shadow mask.
inline std::array<std::array<std::uint32_t,4>,32> native_mtoon_shadow_actor_ranges(
    const std::vector<NativeMToonFrameDraw>& draws,std::uint32_t generation)noexcept {
    std::array<std::array<std::uint32_t,4>,32> out{};
    struct Range {std::uint32_t first=UINT32_MAX,last=0,count=0;};std::array<Range,32> ranges{};
    if(!generation)return out;std::uint32_t rank=0;
    for(const auto& draw:draws){const auto indices=draw.uniform.geometry[3],count=indices/3;
        if(!count||indices%3||rank>0x7fffffffu-count)return {};
        if(!draw.capture_only&&draw.shadow_actor&&draw.shadow_actor<=ranges.size()){
            auto& r=ranges[draw.shadow_actor-1];if(!r.count)r.first=rank;
            r.count+=count;r.last=rank+count;
        }
        rank+=count;
    }
    for(unsigned n=0;n<ranges.size();++n){const auto& r=ranges[n];
        if(r.count&&r.last-r.first==r.count)out[n]={r.first,r.count,generation,n+1u};}
    return out;
}
struct NativeMToonFrameShadeDraw {
    std::uint32_t work_index=0,forward_actor=0,outline_owner=0;bool outline=false;
    NativeMToonShadeInputs inputs; // Owned/material roles only; native roles empty.
};
class NativeMToonFrame;
// Serialized per-device buffer pool, bounded to three retained recording slots
// and512MiB actual allocations. A slot is reusable only after its final frame
// lease retires. Parent proves original GPU completion/reset before that lease
// is released; this class performs no wait, submit or completion inference.
class NativeMToonFramePool {
public:
    static constexpr VkDeviceSize byte_limit=512ull*1024*1024;
    NativeMToonFramePool(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonWorkCapabilities&,const bool* parent_quarantine);
    ~NativeMToonFramePool();
    // Ordinary frames may omit all optional diagnostic sample preparation.
    // The default preserves explicit diagnostic/test callers.
    std::unique_ptr<NativeMToonFrame> prepare(VkCommandBuffer,VkExtent2D,const std::vector<NativeMToonFrameDraw>&,
        std::shared_ptr<const NativeMToonRasterProgram>,std::shared_ptr<const NativeMToonWorkProgram>,bool collect_observations=true,
        const NativeMToonProfileConfig& profile={},const MToonNumericConfig& numeric={});
    VkDeviceSize allocated_bytes()const noexcept;
    std::size_t retained_slots()const noexcept;
    void quarantine()noexcept;
private:
    struct Impl;std::shared_ptr<Impl> impl;
    friend class NativeMToonFrame;
};
// One original view across every actor/material. All main ranks precede all
// outline ranks, in exact draw order; ordinary materials write their own IDs
// with unselected Work records so they retire earlier selected surfaces.
// Immutable descriptors are written once before bind. No native source/output
// descriptor, scene admission, F8 readiness or body/common-commit permit lives
// here. Parent inserts visibility and FINAL matching GBuffer, supplies exact
// dynamic raster state/restore prefix, and retains all inputs through retirement.
// Counts decoded only after independently established original execution
// completion. No texture colours, native handles or rendering authority.
struct NativeMToonGpuObservation {
    std::array<bool,2> numeric_headers{};
    std::vector<MToonNumericSample> numeric_samples;
    bool available=false,header_valid=false,work_valid=false;
    std::uint32_t generation=0,width=0,height=0,work_draws=0,group_work=0,declined=0;
    std::uint32_t selected_work=0,outline_work=0,identity_samples=0,selected_samples=0,outline_samples=0;
    std::uint32_t matching_packets=0,matching_outlines=0,malformed_packets=0;
    std::uint32_t outline_coverage_samples=0,outline_coverage_units=0;
    std::uint32_t forward_fragments=0,forward_colours=0,forward_overflow=0,forward_samples=0,forward_matches=0;
    std::array<std::uint32_t,14> forward_declines{};
    // Diagnostic identity is independent of valid zero-fragment coverage.
    // These values never participate in rendering admission or GPU commands.
    bool forward_present=false,forward_capture_header_valid=false,forward_packet_header_valid=false,forward_active=false;
    std::uint32_t forward_capacity=0,forward_prepared_mask=0,forward_enabled_mask=0;
    std::uint32_t forward_capture_draws=0,forward_enabled_draws=0,forward_shade_controls=0,forward_enabled_shades=0;
    std::array<std::uint32_t,8> forward_capture_header{};
    std::array<std::uint32_t,4> forward_packet_header{};
    // Rejected non-colour tag2, keyed to one selected current grid sample.
    std::array<std::uint32_t,14> shader_declines{};
    std::array<std::uint32_t,8> water_declines{};std::uint32_t water_facts=0;
    std::array<std::uint32_t,8> point_declines{};std::uint32_t point_facts=0;
};
#define EVRM_NATIVE_MTOON_FRAME_OBSERVATION 1
#define EVRM_NATIVE_MTOON_FORWARD_OBSERVATION 1
#define EVRM_NATIVE_MTOON_FRAME_OPTIONAL_OBSERVATION 1
#define EVRM_NATIVE_MTOON_FRAME_DECLINES 1
#define EVRM_NATIVE_MTOON_WATER_DECLINES 1
#define EVRM_NATIVE_MTOON_POINT_DECLINES 1
#define EVRM_NATIVE_MTOON_GPU_PROFILE 1
class NativeMToonFrame {
public:
    ~NativeMToonFrame();
    NativeMToonFrame(const NativeMToonFrame&)=delete;
    NativeMToonFrame& operator=(const NativeMToonFrame&)=delete;
    bool initialize(VkCommandBuffer)noexcept;
    VkDescriptorBufferInfo forward_capture()const noexcept;
    VkDescriptorBufferInfo forward_packets()const noexcept;
    void forward_ready(bool)noexcept;
    // Only this actor has a completed owned forward finish; all others stay ordinary.
    void forward_ready(std::uint32_t actor,bool)noexcept;
    bool has_shadows()const noexcept;
    bool has_outlines()const noexcept;
    // Main draw indices only. Caller supplies a retained compatible352-byte
    // native light set and original shadow rendering/dynamic/restore state.
    // No outline extrusion, allocation, compilation or submit is performed.
    bool record_shadow(VkCommandBuffer,VkDescriptorSet light,const std::uint32_t* draw_indices,std::uint32_t count,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)noexcept;
    bool record_raster(VkCommandBuffer,bool gbuffer,VkPipeline original,VkPipelineLayout compatible,
        std::uint32_t native_set_count,const VkDescriptorSet*)noexcept;
    // Outside rendering, after final IDs. Changes compute bindings; parent
    // restores the entire original compute prefix before forwarding any command.
    bool record_work(VkCommandBuffer)noexcept;
    // Parent supplies separately proven native masks and arms only before the
    // original admitted submission, after complete shade/composition recording.
    // Values alone confer no initialization/use/layout/queue/lifetime authority.
    bool source_masks(std::uint32_t buffers,std::uint32_t images,std::uint32_t samplers,
        std::uint32_t water,bool original_quad_qualified)noexcept;
    bool set_active(bool separately_admitted)noexcept;
    VkDescriptorBufferInfo identities()const noexcept;
    VkDescriptorBufferInfo packets()const noexcept;
    VkDescriptorBufferInfo composite_permit()const noexcept;
    VkDescriptorBufferInfo work_records()const noexcept;
    VkDescriptorBufferInfo work_summary()const noexcept;
    // Optional owned-buffer copies and fixed-grid decline markers, outside
    // rendering after final shade/composition. Arms only these retained owned
    // shade controls before submission; never native access/write authority.
    // No new binding, queue, submission or wait. Returns false when optional
    // sample preparation was disabled for this frame.
    bool record_observation(VkCommandBuffer)noexcept;
    bool numeric_enabled()const noexcept;
    // CALLER MUST establish successful original nonpending execution completion
    // first. The recorded TRANSFER->HOST barrier supplies visibility; coherent
    // mapping alone never supplies completion. Never call from F8/present polling.
    NativeMToonGpuObservation observation_after_original_completion()const noexcept;
    // Diagnostic scopes only. The optional config binds the exact observed
    // command-pool family to the separately selected submit family. These do
    // not change rendering success, bindings, barriers, permits or sources.
    bool begin_gpu_stage(VkCommandBuffer,NativeMToonProfileStage)noexcept;
    bool end_gpu_stage(VkCommandBuffer,NativeMToonProfileStage)noexcept;
    // Raw shade-description group sizes, in opaque/forward/outline order.
    // Backend recording must count only passes it successfully constructed.
    std::array<std::uint32_t,3> shade_group_counts()const noexcept;
    // CALLER proves successful original nonpending completion first. One read
    // at most per sampled frame; unavailable/absent spans have no valid bit.
    NativeMToonProfileResult gpu_profile_after_original_completion()noexcept;
    const std::vector<NativeMToonFrameShadeDraw>& shade_draws()const noexcept;
    // Actual retained forward draws, independently of current per-actor readiness.
    std::uint32_t forward_actor_mask()const noexcept;
    std::uint32_t generation()const noexcept;
    std::uint32_t draw_count()const noexcept;
    void quarantine()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
    NativeMToonFrame(std::shared_ptr<NativeMToonFramePool::Impl>,VkCommandBuffer,VkExtent2D,
        const std::vector<NativeMToonFrameDraw>&,std::shared_ptr<const NativeMToonRasterProgram>,std::shared_ptr<const NativeMToonWorkProgram>,bool collect_observations,const NativeMToonProfileConfig&,const MToonNumericConfig&);
    friend class NativeMToonFramePool;
};
}
