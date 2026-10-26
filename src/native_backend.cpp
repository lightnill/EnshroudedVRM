#define VK_NO_PROTOTYPES
#include "native_backend.hpp"
#include "native_draw.hpp"
#include "native_prepare_contract.hpp"
#include "native_submit.hpp"
#include "native_clip_pipelines.hpp"
#include "native_buffer_memory.hpp"
#include "native_image_memory.hpp"
#include "native_body_coverage.hpp"
#include "native_body_selection.hpp"
#include "native_owner_process.hpp"
#include "native_tool_slots.hpp"
#include "native_instance_qualifiers.hpp"
#include "native_transparency_trace.hpp"
#include "native_material_plan.hpp"
#include "native_skinning_gpu.hpp"
#include "native_volume_snapshot.hpp"
#include "native_oit_draw.hpp"
#include "runtime_log.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <tuple>

namespace evrm {
namespace {
void checked(VkResult r,const char* action){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(action)+" "+std::to_string(r));}
std::string digest_text(const std::array<std::uint8_t,32>& digest){
    constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(64);
    for(const auto byte:digest){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}return result;
}
NativeBodyShaderIdentity body_identity(const NativePipelineState& state){
    NativeBodyShaderIdentity id;id.graphics_stage_count=state.graphics_stage_count;
    id.vertex_bytes=state.vertex_bytes;id.vertex_digest=state.vertex_digest;
    id.mesh_bytes=state.mesh_bytes;id.mesh_digest=state.mesh_digest;
    id.fragment_bytes=state.fragment_bytes;id.fragment_digest=state.fragment_digest;
    id.tess_control_bytes=state.tess_control_bytes;id.tess_control_digest=state.tess_control_digest;
    id.tess_evaluation_bytes=state.tess_evaluation_bytes;id.tess_evaluation_digest=state.tess_evaluation_digest;return id;
}
const char* owner_phase_name(NativeOwnerObservationPhase phase)noexcept{
    constexpr const char* names[]={"none","adapter","image","owners","registry","census","equipment","services","models","capture-verify","submission-verify"};
    const auto index=static_cast<unsigned>(phase);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
const char* owner_failure_name(NativeOwnerFailureLocation location)noexcept{
    constexpr const char* names[]={"none","validation","precheck","range","budget","read","postcheck","compare","finalcheck","internal"};
    const auto index=static_cast<unsigned>(location);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
const char* image_disable_name(NativeImageDisableReason reason)noexcept{
    constexpr const char* names[]={"none","invalid-configuration","generation-exhaustion","revision-exhaustion","allocation-census","image-census","buffer-census","metadata-exception","unknown-memory-or-binding","explicit-unobserved"};
    const auto index=static_cast<unsigned>(reason);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
struct Context {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};VkPhysicalDeviceMemoryProperties memory{};bool uncertain=false;
    template<class T>T fn(const char* name)const{return reinterpret_cast<T>(get(device,name));}
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags)const {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("Native private memory type unavailable");
    }
};
#define F(n) context.fn<PFN_##n>(#n)
struct PrivateBuffer {
    Context& context;VkBuffer handle{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize size=0;
    explicit PrivateBuffer(Context& c):context(c){}
    ~PrivateBuffer(){if(context.uncertain)return;if(mapped)F(vkUnmapMemory)(context.device,memory);if(handle)F(vkDestroyBuffer)(context.device,handle,nullptr);if(memory)F(vkFreeMemory)(context.device,memory,nullptr);}
};
std::unique_ptr<PrivateBuffer> make_buffer(Context& context,VkDeviceSize size,VkBufferUsageFlags usage) {
    auto out=std::make_unique<PrivateBuffer>(context);out->size=size;
    VkBufferCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=size;info.usage=usage;
    checked(F(vkCreateBuffer)(context.device,&info,nullptr,&out->handle),"Native buffer");
    VkMemoryRequirements req{};F(vkGetBufferMemoryRequirements)(context.device,out->handle,&req);
    VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;
    allocation.memoryTypeIndex=context.memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    checked(F(vkAllocateMemory)(context.device,&allocation,nullptr,&out->memory),"Native buffer memory");
    checked(F(vkBindBufferMemory)(context.device,out->handle,out->memory,0),"Native buffer bind");
    checked(F(vkMapMemory)(context.device,out->memory,0,size,0,&out->mapped),"Native buffer map");std::memset(out->mapped,0,static_cast<size_t>(size));return out;
}
struct PrivateImage {
    Context& context;VkImage image{};VkDeviceMemory memory{};VkImageView view{};
    explicit PrivateImage(Context& c):context(c){}
    ~PrivateImage(){if(context.uncertain)return;if(view)F(vkDestroyImageView)(context.device,view,nullptr);if(image)F(vkDestroyImage)(context.device,image,nullptr);if(memory)F(vkFreeMemory)(context.device,memory,nullptr);}
};
struct CompatibleLayout {
    Context& context;VkPipelineLayout handle{};
    explicit CompatibleLayout(Context& c):context(c){}
    ~CompatibleLayout(){if(handle&&!context.uncertain)F(vkDestroyPipelineLayout)(context.device,handle,nullptr);}
};
struct AssetGpu {
    Context& context;std::shared_ptr<const vrm::Avatar> asset;
    std::shared_ptr<NativeSkinGpuAsset> skin;
    std::vector<std::unique_ptr<PrivateImage>> images;std::vector<VkSampler> samplers;std::vector<uint32_t> image_for_material;
    explicit AssetGpu(Context& c):context(c){}
    ~AssetGpu(){if(!context.uncertain)for(auto sampler:samplers)if(sampler)F(vkDestroySampler)(context.device,sampler,nullptr);}
};
struct alignas(16) NativeMaterial {float base[4]{},emissive[4]{},surface[4]{};uint32_t instance[4]{};float alpha[4]{};};
static_assert(sizeof(NativeMaterial)==80,"Native material ABI");
// No Bundle reference: commands retain one previous immutable emission without
// retaining its predecessor, descriptors, or native source resources.
struct SubmittedHistory {
    std::shared_ptr<PrivateBuffer> frame;
    std::shared_ptr<const NativeSkinGpuGeometry> geometry;
    std::shared_ptr<const NativeAvatarPublication> pose;
    NativeOwnerCandidate owner;
    NativeMenuOwnerCandidate menu_owner;
    bool menu_preview=false;
    NativeBufferBinding instances;
    VkExtent2D extent{};
    float probe_mode=0;
};
struct Bundle {
    Context& context;std::shared_ptr<const NativeAvatarPublication> pose;std::shared_ptr<AssetGpu> assets;
    std::unique_ptr<PrivateBuffer> control,vertices,indices,materials;
    std::shared_ptr<NativeSkinGpuFrame> skin;
    std::shared_ptr<PrivateBuffer> frame;
    std::shared_ptr<SubmittedHistory> previous,emission;
    std::unique_ptr<PrivateBuffer> body_bits;
    VkDescriptorPool pool{};VkDescriptorSet prepare{};std::vector<NativeDrawCall> calls;
    VkDescriptorSet clip_set{};
    NativeMaterialPlan material_plan;
    std::unique_ptr<NativeVolumeSnapshot> volume_snapshot;
    std::array<NativeStorageImageBinding,2> volume_storage;
    std::array<NativeImageAllocationBinding,2> volume_allocations;
    NativeBoundState volume_bound;
    struct LateWitness {NativeBoundState bound;NativeBufferBinding constants,forward;std::array<NativeSampledImageBinding,2> images;};
    std::vector<LateWitness> late_witnesses;
    NativeOitWindow oit_window;
    std::array<NativeOitView,5> oit_colors;NativeOitView oit_depth;NativeRasterState oit_raster;
    VkDescriptorPool oit_pool{};std::vector<NativeDrawCall> oit_calls;
    std::shared_ptr<CompatibleLayout> volume_restore;
    VkDeviceSize material_stride=0;
    bool snapshot_attempted=false,snapshot_recorded=false;
    // Observational failure facts only; never readiness or resource leases.
    const char* snapshot_decline="none";
    NativeBoundState snapshot_decline_bound;
    std::uint32_t snapshot_decline_binding=UINT32_MAX;
    VkImage snapshot_decline_image{};
    std::array<std::uint32_t,3> snapshot_dispatch{};VkExtent3D snapshot_extent{};
    NativeOwnerCandidate cpu_owner;NativeMenuOwnerCandidate menu_owner;bool menu_preview=false,menu_cutout=false;NativeBodyMask body_mask;
    // Aliases immutable admitted hint storage; no second guard-vector copy.
    // This is comparison provenance only, never submission authority.
    std::shared_ptr<const NativeOwnerCandidate> prepared_owner_hint;
    std::vector<std::array<std::uint32_t,4>> qualified_parts;
    NativeBufferBinding publication_source;
    std::vector<std::shared_ptr<const NativeClipPipeline>> clip_leases;
    std::vector<NativeBufferBinding> excluded_transforms;
    std::uint32_t clipped_draws=0,visibility_clips=0,gbuffer_clips=0;
    std::vector<NativeBufferBinding> sources;
    std::shared_ptr<CompatibleLayout> compute_restore;
    std::vector<std::shared_ptr<CompatibleLayout>> graphics_restore;
    NativePrepareControl data{};NativePassState first_pass;
    NativeBufferBinding arena,instances,culled,indirect,counts,vis_camera,gbuf_camera,owner;
    uint32_t visibility_passes=0,gbuffer_passes=0,visibility_draws=0,gbuffer_draws=0;
    float probe_mode=0;
    unsigned shadow_probe_draws=0;
    struct ShadowView {NativeBufferBinding source;NativeOitView depth;NativeRasterState raster;VkRect2D area{};VkDescriptorSet set{};};
    VkDescriptorPool shadow_pool{};std::vector<ShadowView> shadow_views;std::vector<NativeDrawCall> shadow_calls;
    bool first_regions_open=true,recorded=false,ended=false,submitted=false,invalid=false;
    bool awaiting_submit_result=false,failed_submit=false;
    bool retry_safe=false;
    bool completion_observed=false,history_completion_observed=false;
    VkQueue submitted_queue{};
    explicit Bundle(Context& c):context(c){}
    ~Bundle(){if(context.uncertain){if(volume_snapshot)volume_snapshot->quarantine();return;}if(shadow_pool)F(vkDestroyDescriptorPool)(context.device,shadow_pool,nullptr);if(oit_pool)F(vkDestroyDescriptorPool)(context.device,oit_pool,nullptr);if(pool)F(vkDestroyDescriptorPool)(context.device,pool,nullptr);}
};
struct Recording {
    bool eligible=false,tainted=false,compute_producer=false,pass_active=false,pass_drawn=false;
    bool affected_pass=false,view_gbuffer=false;
    bool shadow_pass=false;
    NativeOitView shadow_depth;VkRect2D shadow_area{};std::size_t shadow_view_start=0;
    std::uint64_t ownership_window=0,ownership_presentation=0;NativeBufferBinding ownership_instances;
    bool ownership_unknown=false,ownership_closed=false;
    const char* ownership_unknown_reason=nullptr;
    std::vector<NativeBufferBinding> ownership_sources;
    NativePassState pass;std::shared_ptr<Bundle> bundle;
    VkBuffer first_indirect{},first_count{};VkDeviceSize first_offset=0,first_count_offset=0;uint32_t first_max=0,first_stride=0;
    VkPipeline native_restore{};
    // Read-only discovery is independent of every render/ownership decision.
    NativeTransparencyRecording transparency;
    NativeOitView observed_gbuffer_depth;VkExtent2D observed_gbuffer_extent{};
    NativeOitView mixed_gbuffer_depth;VkExtent2D mixed_gbuffer_extent{};
    bool observed_oit_active=false,observation_failed=false;
    unsigned observed_late_variants=0,observed_producers=0,observation_lines=0;
};
bool same_binding(const NativeBufferBinding& a,const NativeBufferBinding& b){return a&&b&&a.info.buffer==b.info.buffer&&a.generation==b.generation&&a.info.offset==b.info.offset&&a.info.range==b.info.range;}
bool same_owner_selection(const NativeOwnerCandidate& a,const NativeOwnerCandidate& b) {
    if(a.image.base!=b.image.base||a.image.size!=b.image.size||a.image.timestamp!=b.image.timestamp||a.image.sha256!=b.image.sha256)return false;
    if(std::tie(a.root,a.client,a.controller,a.main,a.execution,a.registry,a.entity_manager,
        a.entity,a.entity_layout,a.entity_storage,a.equipment,a.entity_id,a.entity_generation,a.entity_slot,a.entity_row,
        a.input_registration,a.equipment_registration,a.render_scene,a.model_manager,a.writer,a.resource_object,
        a.native_buffer,a.native_buffer_bytes,a.instance_capacity,a.instance_extra)!=
       std::tie(b.root,b.client,b.controller,b.main,b.execution,b.registry,b.entity_manager,
        b.entity,b.entity_layout,b.entity_storage,b.equipment,b.entity_id,b.entity_generation,b.entity_slot,b.entity_row,
        b.input_registration,b.equipment_registration,b.render_scene,b.model_manager,b.writer,b.resource_object,
        b.native_buffer,b.native_buffer_bytes,b.instance_capacity,b.instance_extra))return false;
    if(a.models.size()!=b.models.size())return false;
    for(std::size_t i=0;i<a.models.size();++i){const auto& x=a.models[i];const auto& y=b.models[i];
        if(std::tie(x.equipment_slot,x.handle,x.index,x.generation,x.record)!=std::tie(y.equipment_slot,y.handle,y.index,y.generation,y.record))return false;}
    return true;
}
bool same_geometry_layout(const NativeAvatarPose& a,const NativeAvatarPose& b) {
    if(a.vertices.size()!=b.vertices.size()||a.indices!=b.indices||a.draws.size()!=b.draws.size())return false;
    for(size_t i=0;i<a.draws.size();++i)if(std::tie(a.draws[i].first_index,a.draws[i].index_count,a.draws[i].material)!=
        std::tie(b.draws[i].first_index,b.draws[i].index_count,b.draws[i].material))return false;
    for(const auto& v:a.vertices)for(float p:v.position)if(!std::isfinite(p))return false;
    return true;
}
bool same_selection(const SubmittedHistory& previous,const Bundle& current){
    if(previous.menu_preview!=current.menu_preview)return false;
    if(!current.menu_preview)return same_owner_selection(previous.owner,current.cpu_owner);
    return previous.pose&&current.pose&&previous.pose->parts==current.pose->parts&&
        same_native_menu_owner(previous.menu_owner,current.menu_owner);
}
bool publication_material_plan(const NativeAvatarPublication& publication,NativeMaterialPlan& out){
    if(publication.skinning){
        if(!*publication.skinning||publication.geometry!=publication.skinning->asset->layout)return false;
        out=publication.skinning->material_plan;return out.failure==NativeMaterialPlanFailure::none;
    }
    return publication.asset&&publication.geometry&&make_native_material_plan(*publication.asset,*publication.geometry,out);
}
VkDescriptorBufferInfo vertex_binding(const Bundle& b){return b.skin?b.skin->geometry()->binding():VkDescriptorBufferInfo{b.vertices->handle,0,b.vertices->size};}
VkDescriptorBufferInfo index_binding(const Bundle& b){return b.skin?b.assets->skin->indices():VkDescriptorBufferInfo{b.indices->handle,0,b.indices->size};}
}

struct NativeBackend::Impl {
    Context context;NativeState metadata;PFN_vkSetDeviceLoaderData set_loader{};std::filesystem::path shader_directory;
    std::filesystem::path artifact_probe_file;
    std::uint64_t artifact_probe_checked=0;
    unsigned artifact_probe_value=0;
    bool artifact_probe_read=false,artifact_probe_announced=false;
    float artifact_mode(std::uint64_t now)noexcept {
        if(artifact_probe_file.empty())return 0.f;
        if(!artifact_probe_read||now<artifact_probe_checked||now-artifact_probe_checked>=500) {
            artifact_probe_read=true;artifact_probe_checked=now;
            unsigned value=0;
            try {
                std::ifstream input(artifact_probe_file,std::ios::binary);
                char data[3]{};input.read(data,sizeof data);const auto bytes=input.gcount();
                if((bytes==1||(bytes==2&&data[1]=='\n'))&&data[0]>='0'&&data[0]<='5')value=unsigned(data[0]-'0');
            }catch(...){}
            if(!artifact_probe_announced||value!=artifact_probe_value) {
                try{log_event(value==5?"avatar.artifact-probe mode=vrm-shadow-caster":value==4?"avatar.artifact-probe mode=source-shadow-off":value==3?"avatar.artifact-probe mode=geometry-offset":value==2?"avatar.artifact-probe mode=diffuse-only":value==1?"avatar.artifact-probe mode=emissive-only":"avatar.artifact-probe mode=normal-lit");}catch(...){}
                artifact_probe_announced=true;
            }
            artifact_probe_value=value;
        }
        return static_cast<float>(artifact_probe_value);
    }
    VkInstance instance{};VkPhysicalDevice physical{};PFN_vkGetInstanceProcAddr get_instance{};
    std::unique_ptr<NativeClipPipelines> clips;
    NativeBufferMemory memory;
    std::unique_ptr<NativeImageMemory> image_memory;
    bool image_disable_notice=false;
    std::unique_ptr<NativeOwnerProcess> owner_process;
    NativeToolSlots tool_slots;
    // Copied values for private, permit-zero preparation only. Every accepted
    // submission still revalidates the complete retained guard set. Atomic
    // shared ownership also permits clearing after an exception unlocks mutex.
    struct OwnerPreparationHint {NativeOwnerCandidate candidate;NativeBufferBinding source;};
    std::shared_ptr<const OwnerPreparationHint> owner_preparation_hint;
    void discard_owner_hint()noexcept {std::atomic_store(&owner_preparation_hint,std::shared_ptr<const OwnerPreparationHint>{});}
    using HistoryKey=std::tuple<std::uint64_t,std::uint32_t,std::uint32_t>;
    using HistoryBank=std::map<HistoryKey,std::shared_ptr<SubmittedHistory>>;
    std::shared_ptr<const HistoryBank> submitted_histories;
    static HistoryKey history_key(VkBuffer source,VkExtent2D extent){return {reinterpret_cast<std::uintptr_t>(source),extent.width,extent.height};}
    std::shared_ptr<SubmittedHistory> history_for(VkBuffer source,VkExtent2D extent)const {
        const auto bank=std::atomic_load(&submitted_histories);if(!bank)return {};
        const auto found=bank->find(history_key(source,extent));return found==bank->end()?std::shared_ptr<SubmittedHistory>{}:found->second;
    }
    void discard_history()noexcept {std::atomic_store(&submitted_histories,std::shared_ptr<const HistoryBank>{});}
    void update_history(const HistoryKey& key,std::shared_ptr<SubmittedHistory> value){
        const auto previous=std::atomic_load(&submitted_histories);
        auto bank=previous?std::make_shared<HistoryBank>(*previous):std::make_shared<HistoryBank>();
        if(value)(*bank)[key]=std::move(value);else bank->erase(key);
        while(bank->size()>4)bank->erase(bank->begin());
        std::atomic_store(&submitted_histories,std::shared_ptr<const HistoryBank>(std::move(bank)));
    }
    bool independent_view(const Recording& other,const Bundle& b)const {
        if(other.ownership_unknown||!other.ownership_closed||other.ownership_sources.empty())return false;
        for(const auto& source:other.ownership_sources)if(!source||!metadata.live(source)||
            source.info.buffer==b.instances.info.buffer||
            !memory.disjoint(b.instances.info.buffer,0,b.instances.size,source.info.buffer,0,source.size))return false;
        return true;
    }
    void unknown_ownership(Recording& r,const char* reason){
        if(!r.ownership_unknown)r.ownership_unknown_reason=reason;
        r.ownership_unknown=true;
        const auto extent=std::to_string(r.pass.extent.width)+"x"+std::to_string(r.pass.extent.height);
        notice("ownership-incomplete reason="+std::string(reason)+" extent="+extent+
            " tainted="+std::to_string(r.tainted)+" bundle="+std::to_string(static_cast<bool>(r.bundle)),
            "ownership-incomplete-"+std::string(reason)+"-"+extent);
    }
    void revoke_preparation()noexcept {discard_owner_hint();discard_history();tool_slots.disable();}
    void revoke_preparation(const NativeBufferBinding& failed)noexcept {
        try{
            if(context.uncertain||!failed||!metadata.live(failed)||!memory.valid(failed.info.buffer,0,failed.size)){revoke_preparation();return;}
            const auto independent=[&](const NativeBufferBinding& other){
                return other.info.buffer==failed.info.buffer||(other&&metadata.live(other)&&
                    memory.disjoint(failed.info.buffer,0,failed.size,other.info.buffer,0,other.size));
            };
            const auto hint=std::atomic_load(&owner_preparation_hint);
            if(hint&&!independent(hint->source)){revoke_preparation();return;}
            const auto previous=std::atomic_load(&submitted_histories);
            if(previous)for(const auto& entry:*previous)if(!entry.second||!independent(entry.second->instances)){revoke_preparation();return;}
            bool separate=true;each_recording([&](VkCommandBuffer,const Recording& recording){if(recording.bundle&&!independent(recording.bundle->instances))separate=false;});
            if(!separate){revoke_preparation();return;}
            if(hint&&hint->source.info.buffer==failed.info.buffer){discard_owner_hint();tool_slots.disable();}
            if(previous){auto bank=std::make_shared<HistoryBank>(*previous);
                for(auto entry=bank->begin();entry!=bank->end();)if(entry->second->instances.info.buffer==failed.info.buffer)entry=bank->erase(entry);else ++entry;
                std::atomic_store(&submitted_histories,std::shared_ptr<const HistoryBank>(std::move(bank)));}
        }catch(...){revoke_preparation();}
    }
    bool hint_source_current(const OwnerPreparationHint& hint,const NativeBufferBinding& source)const noexcept {
        return same_binding(hint.source,metadata.buffer(source.info.buffer))&&
            hint.source.size==source.size&&hint.source.usage==source.usage&&metadata.live(hint.source)&&
            memory.valid(hint.source.info.buffer,0,hint.source.size);
    }
    void retain_owner_hint(const Bundle& bundle)noexcept {
        if(bundle.menu_preview){if(bundle.menu_owner.kind==NativeMenuOwnerKind::frontend)discard_owner_hint();return;}
        try{
            const auto existing=std::atomic_load(&owner_preparation_hint);
            if(existing&&hint_source_current(*existing,bundle.instances))return;
            auto hint=std::make_shared<OwnerPreparationHint>();
            hint->candidate=bundle.cpu_owner;hint->source=metadata.buffer(bundle.instances.info.buffer);
            if(!hint_source_current(*hint,bundle.instances)){discard_owner_hint();return;}
            std::atomic_store(&owner_preparation_hint,std::shared_ptr<const OwnerPreparationHint>(std::move(hint)));
        }catch(...){discard_owner_hint();} // Optional acceleration cannot veto admission.
    }
#ifdef EVRM_NATIVE_BACKEND_TESTING
    std::shared_ptr<const NativeBackendTestOwner> test_owner_source;
#endif
    bool replacement=false,mesh_enabled=false;
    std::uint64_t coverage_models=0,coverage_independent=0,coverage_uncovered=0,coverage_sources=0,coverage_accepted=0,coverage_declined=0;
    VkPhysicalDeviceLimits limits{};std::mutex mutex;
    std::map<VkCommandBuffer,Recording> commands;
    std::map<VkCommandBuffer,std::vector<Recording>> completed_views;
    template<class F>void each_view(VkCommandBuffer command,F&& visit){
        const auto previous=completed_views.find(command);if(previous!=completed_views.end())for(auto& view:previous->second)visit(command,view);
        const auto current=commands.find(command);if(current!=commands.end())visit(command,current->second);
    }
    template<class F>void each_recording(F&& visit){for(auto& current:commands)each_view(current.first,visit);}
    void retire_views(VkCommandBuffer command,const char* completion){
        each_view(command,[&](VkCommandBuffer native,Recording& recording){harvest_mixed(native,recording,completion);transparency_trace.reset(recording.transparency);});
        completed_views.erase(command);commands.erase(command);
    }
    void next_view(VkCommandBuffer command,const NativePassState& pass){
        auto& recording=commands.at(command);
        if(!replacement||!pass.supported||pass.kind!=NativePipelineKind::visibility||!recording.view_gbuffer||recording.pass_active)return;
        auto& previous=completed_views[command];
        if(previous.size()>=3||recording.native_restore){recording.tainted=true;unknown_ownership(recording,"view-budget-or-restore");return;}
        const bool eligible=recording.eligible,producer=recording.compute_producer;
        previous.push_back(std::move(recording));recording={};recording.eligible=eligible;recording.compute_producer=producer;
        transparency_trace.begin(recording.transparency,command);
    }
    std::map<VkCommandBuffer,VkCommandPool> pools;
    NativeTransparencyTrace transparency_trace;
    unsigned transparency_selection_attempts=0;
    bool transparency_output_ready=false;
    std::map<VkPipelineLayout,std::shared_ptr<CompatibleLayout>> layouts;
    std::shared_ptr<const NativeAvatarPublication> latest;std::shared_ptr<AssetGpu> assets;
    std::shared_ptr<const NativeAvatarPublication> view_publications;
    std::shared_ptr<const NativeAvatarPublication> publication_for(VkBuffer source)const {
        return replacement?native_publication_view(view_publications,reinterpret_cast<std::uintptr_t>(source)):latest;
    }
    void observe_ownership_source(Recording& r,const NativeBufferBinding& source){
        r.ownership_closed=false;
        if(!source||!metadata.live(source)){unknown_ownership(r,"source-not-current");return;}
        if(!r.ownership_instances){
            r.ownership_instances=source;
            if(const auto view=publication_for(source.info.buffer)){r.ownership_window=view->serial;r.ownership_presentation=view->presentation_serial;}
            // An absent pose never removes the primary from its presentation.
        }
        if(std::none_of(r.ownership_sources.begin(),r.ownership_sources.end(),[&](const auto& previous){return same_binding(previous,source);})){
            if(r.ownership_sources.size()>=16){unknown_ownership(r,"source-budget");return;}
            r.ownership_sources.push_back(source);
        }
    }
    void observe_ownership_draw(Recording& r,const NativeBoundState& state){
        if(!r.pass_active||!r.pass.supported)return;
        if(!state.valid){unknown_ownership(r,"draw-state");return;}
        const auto coverage=classify_native_body_coverage(body_identity(state.pipeline));
        if(coverage.kind==NativeBodyCoverageKind::unknown){unknown_ownership(r,"draw-coverage");return;}
        if(coverage.kind==NativeBodyCoverageKind::brick)return;
        if(coverage.kind==NativeBodyCoverageKind::vfx){for(auto binding:{0u,2u})observe_ownership_source(r,metadata.observed_buffer_binding(state,2,binding));}
        else observe_ownership_source(r,metadata.observed_buffer_binding(state,2,
            coverage.kind==NativeBodyCoverageKind::clip?coverage.instance_binding:coverage.transform_binding));
    }
    std::unique_ptr<NativeSkinningGpu> skinning_gpu;
    NativeBufferBinding latest_source;
    VkDescriptorSetLayout draw_set{},prepare_set{};VkPipelineLayout draw_layout{},prepare_layout{};VkPipeline prepare_pipeline{};
    VkShaderModule menu_prepare_shader{};VkPipeline menu_prepare_pipeline{};
    std::array<VkShaderModule,4> shaders{};std::unique_ptr<NativeDrawPipelines> draw_pipelines;
    VkDescriptorSetLayout shadow_set{};VkPipelineLayout shadow_layout{};std::array<VkShaderModule,2> shadow_shaders{};
    std::unique_ptr<NativeShadowDrawPipeline> shadow_pipeline;bool shadow_attempted=false;
    std::uint64_t shadow_caster_views=0,shadow_caster_draws=0;unsigned shadow_contract_notices=0;
    VkDescriptorSetLayout oit_set{};VkPipelineLayout oit_layout{};VkShaderModule oit_shader{};VkSampler volume_sampler{};
    std::unique_ptr<NativeOitDrawPipelines> oit_pipelines;
    NativeVolumeSnapshotCapabilities snapshot_caps;std::vector<std::uint32_t> snapshot_words;
    bool mixed_initialized=false,mixed_failed=false;
    VkCommandPool upload_pool{};VkCommandBuffer upload_command{};VkFence upload_fence{};
    VkQueue queue{};uint32_t family=UINT32_MAX;bool initialized=false,disabled=false,independent_blend=false,partially_bound=false;std::string last_reason;uint64_t permits=0;
    VkResult last_submit_error=VK_ERROR_UNKNOWN;
    unsigned completion_notices=0,completion_observations=0;
    std::uint64_t submission_declines=0;
    unsigned submission_decline_samples=0;
    std::uint64_t shadow_probe_draws=0;
    uint32_t completion_cpu=0,completion_gpu=0;
    uint32_t completion_checkpoint=0,completion_mask=0;
    bool completion_numeric=false;
    std::uint64_t history_completed=0,history_gpu_committed=0,history_gpu_valid=0;
    void harvest_history(Recording& recording,const char* completion)noexcept {
        if(context.uncertain||!recording.bundle)return;
        auto& b=*recording.bundle;
        if(b.history_completion_observed||!b.submitted||b.awaiting_submit_result||b.failed_submit||
           !b.frame||!b.frame->mapped||b.frame->size<sizeof(NativePreparedFrame))return;
        b.history_completion_observed=true;
        std::array<uint32_t,4> gpu{};
        std::memcpy(gpu.data(),static_cast<const unsigned char*>(b.frame->mapped)+offsetof(NativePreparedFrame,control),sizeof gpu);
        ++history_completed;if(gpu[0]==1)++history_gpu_committed;if(gpu[0]==1&&gpu[1]==1)++history_gpu_valid;
        if(b.skin&&b.skin->status_after_completion()!=0)try{log_event("native-skinning.rejected status="+std::to_string(b.skin->status_after_completion())+" pose-serial="+std::to_string(b.pose->serial));}catch(...){}
        if(history_completed<=4||history_completed%300==0)try {
            std::string menu_diagnostic;
            if(b.menu_preview&&b.frame->size>=native_menu_prepared_bytes){
                NativeMenuPrepareDiagnostic value;
                std::memcpy(&value,static_cast<const unsigned char*>(b.frame->mapped)+native_menu_diagnostic_offset,sizeof value);
                if(value.magic==native_menu_diagnostic_magic&&value.checkpoint>=1&&value.checkpoint<=11&&!value.reserved[0]&&!value.reserved[1])
                    menu_diagnostic=" menu-stage="+std::to_string(value.checkpoint);
            }
            log_event("native-history.completed count="+std::to_string(history_completed)+" gpu-committed="+
                std::to_string(history_gpu_committed)+" gpu-history="+std::to_string(history_gpu_valid)+
                " cpu-request="+std::to_string(b.data.history[0])+" gpu-current="+std::to_string(gpu[0])+
                " gpu-previous="+std::to_string(gpu[1])+menu_diagnostic+" completion="+completion);
        }catch(...){}
    }
    // Diagnostic only: callers establish execution completion through an
    // existing successful idle or valid-original nonpending CB retirement.
    // The existing compute->HOST/HOST_READ barrier supplies visibility; the
    // coherent mapping alone is never treated as execution completion.
    void harvest_mixed(VkCommandBuffer command,Recording& recording,const char* completion)noexcept{
        harvest_history(recording,completion);
        if(completion_notices>=16||context.uncertain||!recording.bundle)return;
        auto& b=*recording.bundle;
        if(b.completion_observed||!b.material_plan.mixed||!b.submitted||b.awaiting_submit_result||b.failed_submit||
            !b.frame||!b.frame->mapped||b.frame->size<offsetof(NativePreparedFrame,control)+sizeof(uint32_t))return;
        b.completion_observed=true;
        uint32_t gpu=0;std::memcpy(&gpu,static_cast<const unsigned char*>(b.frame->mapped)+offsetof(NativePreparedFrame,control),sizeof gpu);
        NativeOitPrepareDiagnostic diagnostic;
        if(b.frame->size>=native_oit_diagnostic_offset+sizeof diagnostic)
            std::memcpy(&diagnostic,static_cast<const unsigned char*>(b.frame->mapped)+native_oit_diagnostic_offset,sizeof diagnostic);
        const bool numeric=diagnostic.magic==native_oit_diagnostic_magic&&diagnostic.checkpoint>=1&&diagnostic.checkpoint<=11&&
            !(diagnostic.valid_mask&~15u)&&!diagnostic.reserved&&
            std::all_of(std::begin(diagnostic.spare),std::end(diagnostic.spare),[](uint32_t word){return word==0;});
        const uint32_t cpu=b.data.permit[0];
        const bool emit=completion_observations<4||cpu!=completion_cpu||gpu!=completion_gpu||numeric!=completion_numeric||
            diagnostic.checkpoint!=completion_checkpoint||diagnostic.valid_mask!=completion_mask;
        if(completion_observations<4)++completion_observations;
        completion_cpu=cpu;completion_gpu=gpu;
        completion_numeric=numeric;completion_checkpoint=diagnostic.checkpoint;completion_mask=diagnostic.valid_mask;
        if(!emit)return;
        ++completion_notices;
        try{
            // Fixed owned word counts only; raw bits retain NaN/Inf evidence
            // without interpreting diagnostics as offsets or permit authority.
            const auto words=[](const uint32_t* data,size_t count){std::string result;for(size_t i=0;i<count;++i){if(i)result+=',';result+=std::to_string(data[i]);}return result;};
            std::string numeric_text=" numeric=unavailable";
            if(numeric){
                uint32_t base[3],emissive[3];std::memcpy(base,b.data.blend_base_max,sizeof base);std::memcpy(emissive,b.data.blend_emissive_max,sizeof emissive);
                numeric_text=" numeric=available checkpoint="+std::to_string(diagnostic.checkpoint)+" values-valid="+std::to_string(diagnostic.valid_mask)+
                    " scalar-bits="+words(diagnostic.constants,10)+" w-bits="+words(diagnostic.w,2)+" domain-bits="+words(diagnostic.domain,2)+
                    " radiance-bits="+words(diagnostic.radiance,3)+" extent="+std::to_string(b.data.extent[0])+"x"+std::to_string(b.data.extent[1])+
                    " base-bits="+words(base,3)+" emissive-bits="+words(emissive,3);
            }
            log_event("native-mixed.completed-frame cpu-permit="+std::to_string(cpu)+" gpu-commit="+std::to_string(gpu)+
                " record="+std::to_string(recording.transparency.recording_id)+
                " command="+std::to_string(reinterpret_cast<std::uintptr_t>(command))+
                " pose="+std::to_string(b.pose?b.pose->serial:0)+
                " frame="+std::to_string(reinterpret_cast<std::uintptr_t>(b.frame->handle))+" completion="+completion+numeric_text);
        }catch(...){}
    }
    void observe_image_disable()noexcept{
        if(!image_memory||image_disable_notice)return;
        const auto status=image_memory->diagnostic(VK_NULL_HANDLE);
        if(!status.observed||!status.disabled)return;
        image_disable_notice=true;
        try{
            log_event(std::string("native-image-backing.registry-disabled reason=")+image_disable_name(status.disable_reason)+
                " site="+std::to_string(static_cast<unsigned>(status.disable_reason))+
                " counts="+std::to_string(status.allocations)+","+std::to_string(status.images)+","+std::to_string(status.buffers)+
                " granularity="+std::to_string(status.granularity));
        }catch(...){}
    }
    bool owner_supported()const noexcept {
#ifdef EVRM_NATIVE_BACKEND_TESTING
        if(test_owner_source)return true;
#endif
        return owner_process&&owner_process->supported();
    }
    mutable std::array<double,2> owner_timing_ms{};
    mutable std::array<double,5> resource_timing_ms{};
    mutable std::array<unsigned,5> resource_timing_count{};
    void resource_timing(unsigned index,std::chrono::steady_clock::time_point started)const noexcept {
        resource_timing_ms[index]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(++resource_timing_count[index]==300){
            constexpr const char* names[]={"bundle-total","bundle-resources","bundle-retire","tool-publish","tool-advance"};
            try{log_event(std::string("native-replacement.resource-timing-ms operation=")+names[index]+" average="+std::to_string(resource_timing_ms[index]/300));}catch(...){}
            resource_timing_ms[index]=0;resource_timing_count[index]=0;
        }
    }
    mutable std::array<unsigned,2> owner_timing_count{};
    void owner_timing(unsigned index,std::chrono::steady_clock::time_point started)const noexcept {
        owner_timing_ms[index]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(++owner_timing_count[index]%300==0){
            try{log_event(std::string("native-replacement.owner-timing-ms operation=")+(index?"revalidate":"prepare")+" average="+std::to_string(owner_timing_ms[index]/300));}catch(...){}
            owner_timing_ms[index]=0;
        }
    }
    bool capture_owner(NativeOwnerCandidate& candidate,std::string& reason,NativeOwnerObservationStats* stats,std::uint64_t milliseconds=40)const noexcept {
#ifdef EVRM_NATIVE_BACKEND_TESTING
        if(test_owner_source)return test_owner_source->capture(candidate,reason,stats);
#endif
        NativeOwnerLimits limits;limits.max_milliseconds=milliseconds;
        return owner_process&&owner_process->capture(candidate,reason,limits,stats);
    }
    bool prepare_owner(NativeOwnerCandidate& candidate,std::shared_ptr<const NativeOwnerCandidate>& admitted_hint,const NativeBufferBinding& source,std::string& reason,NativeOwnerObservationStats* stats=nullptr) {
        const auto started=std::chrono::steady_clock::now();
        struct Timing {Impl& impl;std::chrono::steady_clock::time_point started;~Timing(){impl.owner_timing(0,started);}} timing{*this,started};
        if(stats)*stats={};
        admitted_hint.reset();
        const auto hint=std::atomic_load(&owner_preparation_hint);
        if(hint&&hint_source_current(*hint,source)){
            candidate=hint->candidate;admitted_hint=std::shared_ptr<const NativeOwnerCandidate>(hint,&hint->candidate);reason.clear();return true;
        }
        if(hint&&hint_source_current(*hint,hint->source)){
            // A different isolated view cannot use this candidate's buffer.
            // Keep the world hint; its exact mask check will decline this view.
            candidate=hint->candidate;reason.clear();return true;
        }
        discard_owner_hint();
        return capture_owner(candidate,reason,stats);
    }
    bool revalidate_owner(NativeOwnerCandidate& candidate,std::string& reason,NativeOwnerObservationStats* stats=nullptr,std::uint64_t milliseconds=40)const noexcept {
        if(stats)*stats={};
#ifdef EVRM_NATIVE_BACKEND_TESTING
        if(test_owner_source)return test_owner_source->revalidate(candidate,reason,stats);
#endif
        NativeOwnerLimits limits;limits.max_milliseconds=milliseconds;
        const auto started=std::chrono::steady_clock::now();
        const bool result=owner_process&&owner_process->revalidate(candidate,reason,limits,stats);
        owner_timing(1,started);return result;
    }
    bool capture_menu_owner(NativeMenuOwnerCandidate& candidate,std::string& reason)const noexcept {
#ifdef EVRM_NATIVE_BACKEND_TESTING
        if(test_owner_source)return test_owner_source->capture_menu(candidate,reason);
#endif
        NativeOwnerLimits budget;budget.max_milliseconds=40;
        return owner_process&&owner_process->capture_menu(candidate,reason,budget);
    }
    bool revalidate_menu_owner(NativeMenuOwnerCandidate& candidate,std::string& reason)const noexcept {
#ifdef EVRM_NATIVE_BACKEND_TESTING
        if(test_owner_source)return test_owner_source->revalidate_menu(candidate,reason);
#endif
        NativeOwnerLimits budget;budget.max_milliseconds=40;
        return owner_process&&owner_process->revalidate_menu(candidate,reason,budget);
    }
    std::uint64_t census_refreshes=0;
    bool refresh_census_owner(Bundle& bundle,std::uint64_t started,std::string& reason,NativeOwnerObservationStats& stats) {
        if(!bundle.prepared_owner_hint||stats.phase!=NativeOwnerObservationPhase::submission_verify||
            stats.failure_location!=NativeOwnerFailureLocation::compare||stats.guard_capture_phase!=NativeOwnerObservationPhase::census)return false;
        const auto previous=bundle.prepared_owner_hint;
        revoke_preparation();
        const auto remaining=[&]()->std::uint64_t{const auto now=GetTickCount64();return now>=started&&now-started<40?40-(now-started):0;};
        auto budget=remaining();if(!budget){reason="owner census refresh deadline exceeded";return false;}
        NativeOwnerCandidate fresh;NativeOwnerObservationStats fresh_stats;
        if(!capture_owner(fresh,reason,&fresh_stats,budget)){stats=fresh_stats;return false;}
        if(!same_owner_selection(*previous,fresh)){reason="owner census refresh changed prepared selection";return false;}
        budget=remaining();if(!budget){reason="owner census refresh deadline exceeded";return false;}
        if(!revalidate_owner(fresh,reason,&fresh_stats,budget)){stats=fresh_stats;return false;}
        if(!remaining()){reason="owner census refresh deadline exceeded";return false;}
        // Capturing cannot renew source lifetimes, a prepared mask/qualifier,
        // or the immutable pose's original 250ms lifetime.
        if(!sources_current(bundle.sources)||!source_current(bundle.gbuf_camera)||!source_current(bundle.owner)||
            !latest||latest->asset!=bundle.pose->asset||GetTickCount64()<bundle.pose->published_milliseconds||
            GetTickCount64()-bundle.pose->published_milliseconds>250){reason="owner census refresh source or pose expired";return false;}
        if(bundle.material_plan.mixed&&(!mixed_current(bundle)||!bundle.oit_window.complete())){reason="owner census refresh mixed source changed";return false;}
        for(const auto& source:bundle.excluded_transforms)if(!metadata.live(source)||
            !memory.disjoint(bundle.instances.info.buffer,0,bundle.instances.size,source.info.buffer,source.info.offset,source.info.range)){
            reason="owner census refresh excluded source changed";return false;}
        NativeBodyMask mask;std::vector<std::array<std::uint32_t,4>> qualified;
        if(!make_native_body_mask(fresh,bundle.pose->parts,bundle.instances,mask,reason)||mask.words!=bundle.body_mask.words||
            !select_native_owner_qualifiers(fresh,bundle.pose->parts,bundle.pose->instance_qualifiers,qualified,reason)||qualified!=bundle.qualified_parts){
            if(reason.empty())reason="owner census refresh mask or qualifiers changed";
            return false;}
        if(!remaining()){reason="owner census refresh deadline exceeded";return false;}
        bundle.cpu_owner=std::move(fresh);bundle.prepared_owner_hint.reset();reason.clear();
        ++census_refreshes;
        if(census_refreshes==1||census_refreshes%30==0)log_event_lazy([&]{return "native-replacement.owner-census-refreshed count="+std::to_string(census_refreshes)+" elapsed-ms="+std::to_string(GetTickCount64()-started)+" guards="+std::to_string(bundle.cpu_owner.guards.size());});
        return true;
    }
    unsigned owner_failure_notices=0;
    void owner_diagnostic(const char* operation,const NativeOwnerObservationStats& stats)noexcept{
        // A rejected owner table provides no candidate (including the menu).
        // Its original reason is already logged; preserve bounded progress
        // notices for traversal and verification failures.
        if(stats.phase==NativeOwnerObservationPhase::owners&&stats.failure_location==NativeOwnerFailureLocation::validation)return;
        if(stats.phase==NativeOwnerObservationPhase::none||owner_failure_notices>=8)return;
        ++owner_failure_notices;
        try{
            // Calls/bytes are attempted/completed. Diagnostic allocation or
            // logging failure never changes the owner's original decision.
            std::string text=std::string("native-replacement.owner-observation operation=")+(operation?operation:"unknown")+
                " phase="+owner_phase_name(stats.phase)+" failure="+owner_failure_name(stats.failure_location)+
                " calls="+std::to_string(stats.attempted_calls)+"/"+std::to_string(stats.completed_calls)+
                " bytes="+std::to_string(stats.attempted_bytes)+"/"+std::to_string(stats.completed_bytes)+
                " guard="+std::to_string(stats.guard_ordinal)+"/"+std::to_string(stats.guard_total)+
                " elapsed-ms="+std::to_string(stats.elapsed_milliseconds)+" clock-regressed="+(stats.clock_regressed?"1":"0");
            if(stats.failure_location==NativeOwnerFailureLocation::compare)
                text+=" captured="+std::string(owner_phase_name(stats.guard_capture_phase))+" span="+std::to_string(stats.guard_span_bytes)+" difference="+std::to_string(stats.guard_difference_offset);
            if(text.size()>512)text.resize(512);
            log_event(text);
        }catch(...){}
    }
    std::vector<std::string> diagnostic_notices;
    std::vector<VkPipeline> clip_unavailable_notices;
    template<class T>T function(const char* name)const{return context.fn<T>(name);}
#undef F
#define F(n) function<PFN_##n>(#n)
    void report(const std::string& reason){if(last_reason!=reason){log_event("native-additive.unavailable "+reason);last_reason=reason;}}
    bool wants_notice(const std::string& key)const{return diagnostic_notices.size()<24 && std::find(diagnostic_notices.begin(),diagnostic_notices.end(),key)==diagnostic_notices.end();}
    void notice(const std::string& reason,const std::string& key={}){const auto& identity=key.empty()?reason:key;
        if(!wants_notice(identity))return;
        diagnostic_notices.push_back(identity);log_event("native-additive.diagnostic "+reason);}
    void clip_diagnostic(const NativeBoundState& state,const char* phase,const char* clip_set,bool only_missing=false)noexcept {
        try{
            if(!replacement||!clips||!state.valid||clip_unavailable_notices.size()>=8||
               std::find(clip_unavailable_notices.begin(),clip_unavailable_notices.end(),state.pipeline.pipeline)!=clip_unavailable_notices.end())return;
            const auto coverage=classify_native_body_coverage(body_identity(state.pipeline));
            if(coverage.kind!=NativeBodyCoverageKind::clip)return;
            const auto pipeline=clips->pipeline(state.pipeline.pipeline);if(only_missing&&pipeline)return;
            const auto culled=metadata.observed_buffer_binding(state,2,coverage.culled_binding);
            clip_unavailable_notices.push_back(state.pipeline.pipeline);
            log_event("native-replacement.clip-diagnostic phase="+std::string(phase)+" original="+std::to_string(reinterpret_cast<std::uintptr_t>(state.pipeline.pipeline))+
                " culled-valid="+std::to_string(static_cast<bool>(culled))+" usage="+std::to_string(culled.usage)+" range="+std::to_string(culled.info.range)+
                " private-pipeline="+std::to_string(static_cast<bool>(pipeline))+" kind-match="+std::to_string(pipeline&&pipeline->kind==coverage.clip_kind)+
                " layout-match="+std::to_string(pipeline&&pipeline->original_layout==state.pipeline.layout)+" clip-set="+clip_set+
                " | "+clips->diagnostic(state.pipeline.pipeline,state.pipeline.layout));
        }catch(...){/* Observation must not change preparation or draw eligibility. */}
    }
    void transparency_line(Recording& r,const std::string& detail)noexcept {
        if(!r.transparency.selected||r.transparency.ended)return;
        if(r.observation_lines>=64){r.observation_failed=true;return;}
        ++r.observation_lines;
        try{
            auto line="native-transparency.record id="+std::to_string(r.transparency.recording_id)+
                " cmd="+std::to_string(r.transparency.command)+" "+detail;
            if(line.size()>2304){line.resize(2304);r.observation_failed=true;}
            log_event(line);
        }catch(...){r.observation_failed=true;}
    }
    void finish_transparency(Recording& r,VkResult result)noexcept {
        if(r.transparency.selected&&!r.transparency.ended)try{
            log_event("native-transparency.record id="+std::to_string(r.transparency.recording_id)+
                " cmd="+std::to_string(r.transparency.command)+" event=cells-end failed="+std::to_string(r.observation_failed)+
                " lines="+std::to_string(r.observation_lines)+" producers="+std::to_string(r.observed_producers)+
                " late-variants="+std::to_string(r.observed_late_variants));
        }catch(...){r.observation_failed=true;}
        transparency_trace.end(r.transparency,result);
    }
    void transparency_image(Recording& r,const NativeBoundState& bound,unsigned set,unsigned binding)noexcept {
        try{
            auto text="event=image-cell "+metadata.sampled_binding_diagnostic(bound,set,binding);
            const auto source=metadata.observed_sampled_image_binding(bound,set,binding);
            const auto storage=metadata.observed_storage_image_binding(bound,set,binding);
            text+=" storageUsable="+std::to_string(static_cast<bool>(storage))+" storageCurrent="+std::to_string(metadata.live(storage));
            if(source||storage){const auto& v=source?source.view:storage.view;text+=" format="+std::to_string(v.format)+" view-type="+std::to_string(v.view_type)+
                " extent="+std::to_string(v.extent.width)+"x"+std::to_string(v.extent.height)+"x"+std::to_string(v.extent.depth)+
                " usage="+std::to_string(v.usage)+" aspect="+std::to_string(v.subresource.aspectMask)+
                " mip="+std::to_string(v.subresource.baseMipLevel)+"+"+std::to_string(v.subresource.levelCount)+
                " layer="+std::to_string(v.subresource.baseArrayLayer)+"+"+std::to_string(v.subresource.layerCount)+
                " imageFlags="+std::to_string(v.image_flags)+" imageUsage="+std::to_string(v.image_usage)+" formatList=";
                for(unsigned i=0;i<v.image_format_count;++i){if(i)text+=",";text+=std::to_string(v.image_formats[i]);}
                NativeImageBacking backing;const bool backed=image_memory&&image_memory->exclusive(v.image,backing);
                text+=" imageBacking="+std::to_string(backed)+" backingCurrent="+std::to_string(backed&&image_memory->current(backing));
                const auto diagnostic=image_memory?image_memory->diagnostic(v.image):NativeImageMemoryDiagnostic{};
                text+=" registryObserved="+std::to_string(diagnostic.observed)+" registryDisabled="+std::to_string(diagnostic.disabled)+
                    " registryCounts="+std::to_string(diagnostic.allocations)+","+std::to_string(diagnostic.images)+","+std::to_string(diagnostic.buffers)+
                    " backingFacts="+std::to_string(diagnostic.image_known)+","+std::to_string(diagnostic.target)+","+std::to_string(diagnostic.query_safe)+","+
                    std::to_string(diagnostic.bind_attempted)+","+std::to_string(diagnostic.footprint_known)+","+std::to_string(diagnostic.allocation_known)+","+
                    std::to_string(diagnostic.allocation_ordinary)+","+std::to_string(diagnostic.allocation_uncertain)+
                    " properties="+std::to_string(diagnostic.memory_properties)+" conflicts="+std::to_string(diagnostic.image_conflicts)+","+std::to_string(diagnostic.buffer_conflicts)+
                    " granularity="+std::to_string(diagnostic.granularity)+" rawMemory="+std::to_string(reinterpret_cast<std::uintptr_t>(diagnostic.memory))+
                    " rawOffset="+std::to_string(diagnostic.offset)+" rawRequirements="+std::to_string(diagnostic.bytes)+" allocationBytes="+std::to_string(diagnostic.allocation_bytes)+
                    " boundCurrentMemGeneration="+std::to_string(diagnostic.bound_memory_generation)+","+std::to_string(diagnostic.current_memory_generation);
                if(backed)text+=" memory="+std::to_string(reinterpret_cast<std::uintptr_t>(backing.memory))+
                    " backingImageGeneration="+std::to_string(backing.image_generation)+" memoryGeneration="+std::to_string(backing.memory_generation)+
                    " overlapRevision="+std::to_string(backing.overlap_revision)+" memoryOffset="+std::to_string(backing.offset)+
                    " requirementBytes="+std::to_string(backing.bytes)+" memoryType="+std::to_string(backing.memory_type_index);}
            transparency_line(r,text);
        }catch(...){r.observation_failed=true;}
    }
    void transparency_buffer(Recording& r,const NativeBoundState& bound,unsigned set,unsigned binding)noexcept {
        try{
            const auto buffer=metadata.observed_buffer_binding(bound,set,binding);
            auto text="event=buffer-cell setIndex="+std::to_string(set)+" binding="+std::to_string(binding)+" "+metadata.buffer_binding_diagnostic(bound,set,binding)+
                " observedBuffer="+std::to_string(static_cast<bool>(buffer))+" currentBuffer="+std::to_string(metadata.live(buffer))+" flags="+std::to_string(buffer.declaration_flags);
            if(buffer)text+=" buffer="+std::to_string(reinterpret_cast<std::uintptr_t>(buffer.info.buffer))+" offset="+std::to_string(buffer.info.offset)+
                " range="+std::to_string(buffer.info.range)+" size="+std::to_string(buffer.size)+" usage="+std::to_string(buffer.usage)+
                " generation="+std::to_string(buffer.generation)+" bufferBacking="+std::to_string(memory.valid(buffer.info.buffer,buffer.info.offset,buffer.info.range));
            transparency_line(r,text);
        }catch(...){r.observation_failed=true;}
    }
    void observe_volume(Recording& r,VkCommandBuffer command,bool direct,unsigned x,unsigned y,unsigned z)noexcept {
        try{
            if(!replacement||!transparency_output_ready||!direct||!x||!y||!z||!r.bundle||r.bundle->invalid||
               r.tainted||!r.eligible||(!r.transparency.selected&&transparency_selection_attempts>=3))return;
            const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);if(!bound.valid)return;
            const auto kind=native_volume_producer(bound.pipeline.compute_bytes,bound.pipeline.compute_digest);
            if(kind==NativeVolumeProducer::unsupported)return;
            if(!r.transparency.selected){
                if(kind!=NativeVolumeProducer::populate)return;
                ++transparency_selection_attempts;
                if(!transparency_trace.select(r.transparency))return;
            }
            const unsigned bit=1u<<static_cast<unsigned>(kind);if(r.observed_producers&bit)return;
            r.observed_producers|=bit;
            const char* name=kind==NativeVolumeProducer::populate?"populate":kind==NativeVolumeProducer::in_scatter?"in-scatter":"integrate";
            transparency_line(r,"event=producer kind="+std::string(name)+" groups="+std::to_string(x)+"x"+std::to_string(y)+"x"+std::to_string(z)+
                " pipeline="+std::to_string(reinterpret_cast<std::uintptr_t>(bound.pipeline.pipeline))+" pipeline-generation="+std::to_string(bound.pipeline.generation)+
                " shader="+std::to_string(bound.pipeline.compute_bytes)+":"+digest_text(bound.pipeline.compute_digest));
            if(kind==NativeVolumeProducer::populate){transparency_image(r,bound,2,3);transparency_image(r,bound,2,4);}
            else if(kind==NativeVolumeProducer::in_scatter){transparency_image(r,bound,0,5);transparency_image(r,bound,0,6);}
        }catch(...){r.observation_failed=true;}
    }
    void observe_rendering(Recording& r,const VkRenderingInfo& info)noexcept {
        r.observed_oit_active=false;
        try{
            if(r.pass.supported&&r.pass.kind==NativePipelineKind::gbuffer){
                r.observed_gbuffer_depth=metadata.attachment_view(r.pass.depth);r.observed_gbuffer_extent=r.pass.extent;
            }
            if(!r.transparency.selected||r.transparency.ended||info.sType!=VK_STRUCTURE_TYPE_RENDERING_INFO||info.pNext||info.flags||
               info.colorAttachmentCount!=5||!info.pColorAttachments||!info.pDepthAttachment)return;
            std::array<NativeOitView,5> colors{};
            for(unsigned i=0;i<5;++i)colors[i]=metadata.attachment_view(info.pColorAttachments[i].imageView);
            const auto depth=metadata.attachment_view(info.pDepthAttachment->imageView);
            r.observed_oit_active=native_oit_final_pass(info,colors,depth,r.observed_gbuffer_depth,r.observed_gbuffer_extent);
            transparency_line(r,"event=oit-window matched="+std::to_string(r.observed_oit_active)+" extent="+
                std::to_string(info.renderArea.extent.width)+"x"+std::to_string(info.renderArea.extent.height)+
                " depth="+std::to_string(reinterpret_cast<std::uintptr_t>(depth.view)));
        }catch(...){r.observation_failed=true;}
    }
    void observe_transparent_draw(Recording& r,VkCommandBuffer command)noexcept {
        try{
            if(!r.transparency.selected||r.transparency.ended||!r.observed_oit_active||r.observed_late_variants==14)return;
            const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
            if(!bound.valid||!bound.pipeline.oit_compatible)return;
            const auto kind=native_oit_late_shader(bound.pipeline);if(kind==NativeOitShader::unsupported)return;
            const auto bit=1u<<static_cast<unsigned>(kind);if(r.observed_late_variants&bit)return;r.observed_late_variants|=bit;
            transparency_line(r,"event=late-state kind="+std::to_string(static_cast<unsigned>(kind))+" pipeline="+
                std::to_string(reinterpret_cast<std::uintptr_t>(bound.pipeline.pipeline))+" pipeline-generation="+std::to_string(bound.pipeline.generation));
            for(const auto set: {0u,3u})transparency_buffer(r,bound,set,11);
            transparency_image(r,bound,0,0);for(unsigned binding=22;binding<=28;++binding)transparency_image(r,bound,3,binding);
        }catch(...){r.observation_failed=true;}
    }
    std::shared_ptr<CompatibleLayout> compatible(const NativeBoundState& state) {
        const auto found=layouts.find(state.pipeline.layout);return found==layouts.end()?nullptr:found->second;
    }
#include "native_mixed_backend.inl"
    VkShaderModule shader(const char* filename) {
        std::ifstream input(shader_directory/filename,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Missing native shader");
        const auto size=input.tellg();if(size<20||size>1024*1024||size%4)throw std::runtime_error("Native shader size");
        std::vector<uint32_t> words(static_cast<size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
        if(!input||words[0]!=0x07230203u)throw std::runtime_error("Native shader bytes");
        VkShaderModuleCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;info.codeSize=words.size()*4;info.pCode=words.data();VkShaderModule result{};
        checked(F(vkCreateShaderModule)(context.device,&info,nullptr,&result),"Native shader");return result;
    }
    void initialize() {
        if(initialized)return;
        if(!independent_blend)throw std::runtime_error("Native independentBlend was not enabled");
        if(limits.maxPerStageDescriptorUniformBuffers<65||limits.maxDescriptorSetUniformBuffers<65||limits.maxUniformBufferRange<65536||
           limits.maxPerStageDescriptorStorageBuffers<6||limits.maxDescriptorSetStorageBuffers<6||
           !limits.minUniformBufferOffsetAlignment||65536%limits.minUniformBufferOffsetAlignment||limits.maxStorageBufferRange<16*1024*1024)
            throw std::runtime_error("Native descriptor limits unsupported");
        const std::array<VkDescriptorSetLayoutBinding,6> bindings={{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}}};
        VkDescriptorSetLayoutCreateInfo set_info{};set_info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set_info.bindingCount=bindings.size();set_info.pBindings=bindings.data();
        checked(F(vkCreateDescriptorSetLayout)(context.device,&set_info,nullptr,&draw_set),"Native draw set layout");
        std::array<VkDescriptorSetLayoutBinding,71> prep{};
        for(uint32_t i=0;i<prep.size();++i)prep[i]={i,i==0||(i>=6&&i<70)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        // Only the observed native culled-list declaration may be partial.
        // This is not UPDATE_AFTER_BIND; recorded private sets stay unchanged.
        std::array<VkDescriptorBindingFlags,71> prep_flags{};
        prep_flags[2]=partially_bound?VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT:0;
        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags{};binding_flags.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        binding_flags.bindingCount=prep_flags.size();binding_flags.pBindingFlags=prep_flags.data();
        set_info.pNext=partially_bound?&binding_flags:nullptr;
        set_info.bindingCount=prep.size();set_info.pBindings=prep.data();checked(F(vkCreateDescriptorSetLayout)(context.device,&set_info,nullptr,&prepare_set),"Native prepare set layout");
        VkPipelineLayoutCreateInfo layout_info{};layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout_info.setLayoutCount=1;layout_info.pSetLayouts=&draw_set;
        checked(F(vkCreatePipelineLayout)(context.device,&layout_info,nullptr,&draw_layout),"Native draw layout");layout_info.pSetLayouts=&prepare_set;
        checked(F(vkCreatePipelineLayout)(context.device,&layout_info,nullptr,&prepare_layout),"Native prepare layout");
        shaders[0]=shader("avatar_native.pull.vert.spv");shaders[1]=shader("avatar_native.pull.visibility.spv");shaders[2]=shader("avatar_native.pull.gbuffer.spv");shaders[3]=shader("native_prepare.comp.spv");
        draw_pipelines=std::make_unique<NativeDrawPipelines>(context.device,context.get,draw_layout,shaders[0],shaders[1],shaders[2]);
        VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=prepare_layout;
        pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shaders[3];pipeline.stage.pName="cs_prepare";
        checked(F(vkCreateComputePipelines)(context.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&prepare_pipeline),"Native prepare pipeline");
        if(replacement&&artifact_probe_file.empty())initialize_shadow();
        initialized=true;
    }
    void initialize_menu_prepare(){
        if(menu_prepare_pipeline)return;
        if(!menu_prepare_shader)menu_prepare_shader=shader("native_menu_prepare.comp.spv");
        VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=prepare_layout;
        pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module=menu_prepare_shader;pipeline.stage.pName="cs_prepare";
        checked(F(vkCreateComputePipelines)(context.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&menu_prepare_pipeline),"Native menu prepare pipeline");
    }
    std::shared_ptr<AssetGpu> upload(const std::shared_ptr<const vrm::Avatar>& avatar) {
        if(avatar->materials.size()>4096||avatar->images.size()>4096)throw std::runtime_error("Native material budget");
        if(!upload_pool) {
            VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            checked(F(vkCreateCommandPool)(context.device,&pool,nullptr,&upload_pool),"Native upload pool");
            VkCommandBufferAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate.commandPool=upload_pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
            checked(F(vkAllocateCommandBuffers)(context.device,&allocate,&upload_command),"Native upload command");if(set_loader)checked(set_loader(context.device,upload_command),"Native upload loader data");
            VkFenceCreateInfo fence{};fence.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;checked(F(vkCreateFence)(context.device,&fence,nullptr,&upload_fence),"Native upload fence");
        }
        auto out=std::make_shared<AssetGpu>(context);out->asset=avatar;
        std::vector<const vrm::Image*> pixels;vrm::Image white{1,1,{255,255,255,255}};pixels.push_back(&white);
        std::map<int,uint32_t> source_images;
        for(const auto& material:avatar->materials)if(material.base_texture>=0){const auto& tex=avatar->textures.at(static_cast<size_t>(material.base_texture));if(!source_images.count(tex.image)){source_images[tex.image]=static_cast<uint32_t>(pixels.size());pixels.push_back(&avatar->images.at(static_cast<size_t>(tex.image)));}}
        std::vector<std::unique_ptr<PrivateBuffer>> staging;
        checked(F(vkResetCommandPool)(context.device,upload_pool,0),"Native upload reset");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(F(vkBeginCommandBuffer)(upload_command,&begin),"Native upload begin");
        if(latest&&latest->skinning){
            if(!skinning_gpu)skinning_gpu=std::make_unique<NativeSkinningGpu>(context.device,context.get,context.memory,limits,shader_directory,&context.uncertain);
            out->skin=skinning_gpu->asset(latest->skinning->asset);skinning_gpu->upload(upload_command,out->skin);
        }
        for(const auto* image:pixels) {
            if(image->width<=0||image->height<=0||image->rgba.size()!=static_cast<size_t>(image->width)*image->height*4)throw std::runtime_error("Native texture dimensions");
            auto target=std::make_unique<PrivateImage>(context);
            VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;info.format=VK_FORMAT_R8G8B8A8_SRGB;info.extent={static_cast<uint32_t>(image->width),static_cast<uint32_t>(image->height),1};
            info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            checked(F(vkCreateImage)(context.device,&info,nullptr,&target->image),"Native texture");VkMemoryRequirements req{};F(vkGetImageMemoryRequirements)(context.device,target->image,&req);
            VkMemoryAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocate.allocationSize=req.size;allocate.memoryTypeIndex=context.memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            checked(F(vkAllocateMemory)(context.device,&allocate,nullptr,&target->memory),"Native texture memory");checked(F(vkBindImageMemory)(context.device,target->image,target->memory,0),"Native texture bind");
            VkImageViewCreateInfo view{};view.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;view.image=target->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=info.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            checked(F(vkCreateImageView)(context.device,&view,nullptr,&target->view),"Native texture view");
            auto buffer=make_buffer(context,image->rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memcpy(buffer->mapped,image->rgba.data(),image->rgba.size());
            VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=target->image;barrier.subresourceRange=view.subresourceRange;
            F(vkCmdPipelineBarrier)(upload_command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=info.extent;
            F(vkCmdCopyBufferToImage)(upload_command,buffer->handle,target->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            F(vkCmdPipelineBarrier)(upload_command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            out->images.push_back(std::move(target));staging.push_back(std::move(buffer));
        }
        const auto wrap=[](int x){return x==33071?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:x==33648?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:VK_SAMPLER_ADDRESS_MODE_REPEAT;};
        for(size_t i=0;i<=avatar->materials.size();++i) {
            const auto index=i<avatar->materials.size()?avatar->materials[i].base_texture:-1;const auto* tex=index>=0?&avatar->textures.at(static_cast<size_t>(index)):nullptr;
            VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.magFilter=tex&&tex->mag_filter==9728?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
            sampler.minFilter=tex&&(tex->min_filter==9728||tex->min_filter==9984||tex->min_filter==9986)?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
            sampler.addressModeU=tex?wrap(tex->wrap_s):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeV=tex?wrap(tex->wrap_t):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkSampler handle{};checked(F(vkCreateSampler)(context.device,&sampler,nullptr,&handle),"Native sampler");out->samplers.push_back(handle);out->image_for_material.push_back(tex?source_images.at(tex->image):0);
        }
        checked(F(vkEndCommandBuffer)(upload_command),"Native upload end");checked(F(vkResetFences)(context.device,1,&upload_fence),"Native upload fence reset");
        VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&upload_command;
        const auto submitted=F(vkQueueSubmit)(queue,1,&submit,upload_fence);
        if(submitted!=VK_SUCCESS){context.uncertain=submitted!=VK_ERROR_DEVICE_LOST;if(context.uncertain&&clips)clips->quarantine();checked(submitted,"Native upload submit");}
        const auto completed=F(vkWaitForFences)(context.device,1,&upload_fence,VK_TRUE,10'000'000'000ull);
        if(completed!=VK_SUCCESS){context.uncertain=completed!=VK_ERROR_DEVICE_LOST;if(context.uncertain&&clips)clips->quarantine();checked(completed,"Native upload completion");}
        if(out->skin)skinning_gpu->finish_upload(out->skin);
        return out;
    }
    bool source_current(const NativeBufferBinding& source) const {
        return metadata.live(source)&&(!(replacement||partially_bound)||
            memory.valid(source.info.buffer,source.info.offset,source.info.range));
    }
    bool sources_current(const std::vector<NativeBufferBinding>& sources) const {
        for(const auto& source:sources)if(!source_current(source))return false;
        return true;
    }
    std::shared_ptr<Bundle> make_bundle(Recording& recording,VkCommandBuffer command) {
        struct Preparation {
            Impl& impl;NativeBufferBinding source;bool complete=false;
            ~Preparation(){if(impl.replacement&&!complete)impl.revoke_preparation(source);}
        } preparation{*this,{}};
        const auto bundle_started=std::chrono::steady_clock::now();
        if(replacement)latest=view_publications;
        const auto extent=std::to_string(recording.pass.extent.width)+"x"+std::to_string(recording.pass.extent.height);
        if(!initialized||!latest||!assets||latest->asset!=assets->asset||context.uncertain||commands.size()>512){
            notice("context-lifecycle extent="+extent+" initialized="+std::to_string(initialized)+" pose="+std::to_string(static_cast<bool>(latest))+" assets="+std::to_string(static_cast<bool>(assets))+" same-asset="+std::to_string(latest&&assets&&latest->asset==assets->asset)+" uncertain="+std::to_string(context.uncertain)+" commands="+std::to_string(commands.size()),"context-lifecycle-"+extent);return{};}
        size_t active=0;each_recording([&](VkCommandBuffer,const Recording& item){if(item.bundle)++active;});if(active>=16){notice("context-bundle-budget active="+std::to_string(active));return{};}
        const auto graphics=metadata.bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS),compute=metadata.bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(!graphics.valid||graphics.pipeline.kind!=NativePipelineKind::visibility||!compute.valid||!compatible(graphics)||!compatible(compute)){
            const auto key="restoration-state-"+std::to_string(recording.pass.extent.width)+"x"+std::to_string(recording.pass.extent.height);
            if(wants_notice(key))notice("restoration-state graphics="+std::to_string(graphics.valid)+" compute="+std::to_string(compute.valid)+" graphics-layout="+std::to_string(static_cast<bool>(compatible(graphics)))+" compute-layout="+std::to_string(static_cast<bool>(compatible(compute)))+" "+metadata.binding_diagnostic(command,VK_PIPELINE_BIND_POINT_COMPUTE),key);
            return{};}
        if(replacement){
            const auto source=metadata.buffer_binding(graphics,2,4);
            preparation.source=source;
            latest=publication_for(source.info.buffer);latest_source=metadata.buffer(source.info.buffer);
            if(!latest){report("no current publication for this native view allocation");return{};}
        }
        auto bundle=std::make_shared<Bundle>(context);bundle->pose=latest;bundle->assets=assets;bundle->compute_restore=compatible(compute);bundle->first_pass=recording.pass;
        if(!publication_material_plan(*latest,bundle->material_plan)){report("native material partition declined");return{};}
        if(bundle->material_plan.mixed&&!replacement){report("mixed path requires shared replacement commit");return{};}
        bundle->instances=metadata.buffer_binding(graphics,2,4);bundle->culled=metadata.buffer_binding(graphics,2,5);bundle->vis_camera=metadata.buffer_binding(graphics,1,4);
        bundle->indirect=metadata.buffer(recording.first_indirect);bundle->counts=metadata.buffer(recording.first_count);bundle->arena=metadata.buffer(bundle->vis_camera.info.buffer);
        if(!bundle->instances||!bundle->culled||!bundle->vis_camera||!bundle->indirect||!bundle->counts||!bundle->arena||
           bundle->instances.info.offset||bundle->instances.info.range==VK_WHOLE_SIZE||bundle->instances.info.range%128||bundle->instances.info.range/128>131072||
           !(bundle->instances.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||!(bundle->culled.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||
           !(bundle->indirect.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||!(bundle->counts.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||
           bundle->instances.info.range>limits.maxStorageBufferRange||bundle->culled.info.range>limits.maxStorageBufferRange||
           bundle->indirect.info.range>limits.maxStorageBufferRange||bundle->counts.info.range>limits.maxStorageBufferRange||
           !(bundle->arena.usage&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)||(bundle->arena.usage&~(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT))||
           bundle->arena.size>4*1024*1024||bundle->arena.size<65536||bundle->arena.size%65536||bundle->vis_camera.info.offset>UINT32_MAX||
           !latest->geometry||latest->geometry->vertices.empty()||latest->geometry->indices.empty()||latest->parts.empty()||latest->parts.size()>64){
            const auto key="context-sources-"+extent;if(wants_notice(key)){
                const auto describe=[](const NativeBufferBinding& b){return std::to_string(static_cast<bool>(b))+":"+std::to_string(b.info.offset)+","+std::to_string(b.info.range)+","+std::to_string(b.size)+","+std::to_string(b.usage);};
                notice("context-sources extent="+extent+" (valid:offset,range,size,usage) instances="+describe(bundle->instances)+" culled="+describe(bundle->culled)+" camera="+describe(bundle->vis_camera)+" arena="+describe(bundle->arena)+" indirect="+describe(bundle->indirect)+" counts="+describe(bundle->counts)+" parts="+std::to_string(latest->parts.size())+" geometry="+std::to_string(static_cast<bool>(latest->geometry))+" | "+metadata.buffer_binding_diagnostic(graphics,2,4)+" | "+metadata.buffer_binding_diagnostic(graphics,2,5)+" | "+metadata.buffer_binding_diagnostic(graphics,1,4),key);
            }return{};}
        bundle->sources={bundle->instances,bundle->culled,bundle->vis_camera,bundle->indirect,bundle->counts,bundle->arena};
        // Instance/camera descriptors retain the ordinary native contract.
        // The only optional source is culled4; the prepare shader proves a
        // complete triangle in the first native call before accessing it.
        if(bundle->instances.declaration_flags||bundle->vis_camera.declaration_flags||
           (bundle->culled.declaration_flags&&
            (bundle->culled.declaration_flags!=VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT||!partially_bound))) {
            report("first-draw source declaration unsupported instances="+
                std::to_string(bundle->instances.declaration_flags)+" culled="+std::to_string(bundle->culled.declaration_flags)+
                " camera="+std::to_string(bundle->vis_camera.declaration_flags)+" partial-enabled="+std::to_string(partially_bound));return{};
        }
        if(!sources_current(bundle->sources)){report("first-draw source backing unavailable");return{};}
        if(replacement) {
            if(!clips||!clips->private_layout()){revoke_preparation();report("replacement clipping layout unavailable");return{};}
            if(!owner_supported()){
                revoke_preparation();report("replacement owner context unavailable: "+(owner_process?owner_process->reason():std::string("adapter missing")));return{};
            }
            std::string reason;
            NativeOwnerObservationStats owner_stats;
            const bool owner_captured=prepare_owner(bundle->cpu_owner,bundle->prepared_owner_hint,bundle->instances,reason,&owner_stats);
            bool selected=false;
            if(owner_captured){
                selected=make_native_body_mask(bundle->cpu_owner,latest->parts,bundle->instances,bundle->body_mask,reason)&&
                    select_native_owner_qualifiers(bundle->cpu_owner,latest->parts,latest->instance_qualifiers,bundle->qualified_parts,reason);
            }
            if(!selected){
                std::string menu_reason;
                if(capture_menu_owner(bundle->menu_owner,menu_reason)){
                    bundle->menu_preview=true;
                    if(bundle->menu_owner.kind==NativeMenuOwnerKind::frontend)tool_slots.disable();
                    initialize_menu_prepare();
                    selected=make_native_menu_body_mask(bundle->menu_owner,latest->parts,latest->instance_qualifiers,
                        bundle->instances,bundle->body_mask,bundle->qualified_parts,reason);
                    if(selected)notice(bundle->menu_owner.kind==NativeMenuOwnerKind::character?
                        "Character renderer allocation selected; no gameplay equipment writes":
                        "frontend menu preview selected; no gameplay equipment writes","menu-preview-ready");
                }
            }
            if(!selected||!memory.valid(bundle->instances.info.buffer,0,bundle->instances.size)) {
                report("replacement owner link declined: "+reason);
                if(!owner_captured)owner_diagnostic("capture",owner_stats);
                return{};
            }
            bundle->publication_source=latest_source;
            if(!metadata.live(bundle->publication_source)||bundle->publication_source.info.buffer!=bundle->instances.info.buffer||
                bundle->publication_source.generation!=bundle->instances.generation) {report("replacement publication source generation changed");return{};}
            bundle->sources.push_back(bundle->publication_source);
            notice("replacement-owner-linked models="+std::to_string(bundle->body_mask.selected)+" capture-ms="+
                std::to_string(bundle->cpu_owner.elapsed_milliseconds)+" reads="+std::to_string(bundle->cpu_owner.read_calls),"replacement-owner-cost");
        }
        // Menu scenes need not produce the world fog volumes or late OIT pass.
        // Only a guarded menu allocation gets this per-bundle cutout partition;
        // the immutable asset/publication retain BLEND for simultaneous world views.
        if(bundle->menu_preview&&bundle->material_plan.mixed){
            auto& plan=bundle->material_plan;
            plan.opaque_draws.insert(plan.opaque_draws.end(),plan.blend_draws.begin(),plan.blend_draws.end());
            std::sort(plan.opaque_draws.begin(),plan.opaque_draws.end());
            plan.blend_draws.clear();plan.mixed=false;bundle->menu_cutout=true;
            notice("menu transparent materials use cutout preview; world BLEND unchanged","menu-cutout-preview");
        }
        if(bundle->material_plan.mixed)initialize_mixed();
        const auto resources_started=std::chrono::steady_clock::now();
        bundle->control=make_buffer(context,sizeof(NativePrepareControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        bundle->frame=make_buffer(context,bundle->material_plan.mixed?native_oit_prepared_bytes:
            bundle->menu_preview?native_menu_prepared_bytes:sizeof(NativePreparedFrame),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        if(!latest->skinning){
            bundle->vertices=make_buffer(context,latest->geometry->vertices.size()*sizeof(NativeAvatarVertex),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            bundle->indices=make_buffer(context,latest->geometry->indices.size()*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            if(bundle->vertices->size>limits.maxStorageBufferRange||bundle->indices->size>limits.maxStorageBufferRange)return{};
            std::memcpy(bundle->vertices->mapped,latest->geometry->vertices.data(),static_cast<size_t>(bundle->vertices->size));std::memcpy(bundle->indices->mapped,latest->geometry->indices.data(),static_cast<size_t>(bundle->indices->size));
        }else if(!skinning_gpu||!assets->skin){report("GPU skin asset unavailable");return{};}
        const auto alignment=std::max<VkDeviceSize>(16,limits.minUniformBufferOffsetAlignment);const auto stride=(sizeof(NativeMaterial)+alignment-1)/alignment*alignment;
        bundle->material_stride=stride;
        const auto material_count=assets->asset->materials.size()+1;bundle->materials=make_buffer(context,stride*material_count,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        const auto probe_mode=artifact_mode(GetTickCount64());
        bundle->probe_mode=probe_mode;
        const auto previous=history_for(bundle->instances.info.buffer,recording.pass.extent);
        if(previous&&previous->pose&&previous->frame&&previous->pose->asset==latest->asset&&
           (latest->presentation_serial?
                previous->pose->presentation_serial&&previous->pose->presentation_serial<UINT64_MAX&&previous->pose->presentation_serial+1==latest->presentation_serial:
                previous->pose->serial<UINT64_MAX&&previous->pose->serial+1==latest->serial)&&
           same_binding(previous->instances,bundle->instances)&&same_selection(*previous,*bundle)&&
           previous->extent.width==recording.pass.extent.width&&previous->extent.height==recording.pass.extent.height&&
           previous->pose->height_scale==latest->height_scale&&previous->pose->basis_xz==latest->basis_xz&&
           previous->pose->diagnostic_x==latest->diagnostic_x&&previous->probe_mode==probe_mode&&
           previous->pose->geometry&&
           (latest->skinning?(previous->pose->skinning&&previous->geometry&&previous->pose->geometry==latest->geometry&&previous->pose->skinning->asset==latest->skinning->asset):
                (!previous->pose->skinning&&same_geometry_layout(*previous->pose->geometry,*latest->geometry)))) {
            bundle->previous=previous;
            if(!latest->skinning){
                auto* vertices=static_cast<NativeAvatarVertex*>(bundle->vertices->mapped);
                for(size_t i=0;i<latest->geometry->vertices.size();++i)
                    std::copy_n(previous->pose->geometry->vertices[i].position,3,vertices[i].previous_position);
            }
        }
        if(latest->skinning)bundle->skin=skinning_gpu->frame(assets->skin,*latest->skinning,{bundle->frame->handle,0,bundle->frame->size},bundle->previous?bundle->previous->geometry:nullptr);
        try {
            bundle->emission=std::make_shared<SubmittedHistory>();bundle->emission->frame=bundle->frame;
            if(bundle->skin)bundle->emission->geometry=bundle->skin->geometry();
            bundle->emission->pose=bundle->pose;bundle->emission->instances=bundle->instances;
            bundle->emission->extent=recording.pass.extent;bundle->emission->probe_mode=probe_mode;
        }catch(...){bundle->emission.reset();} // Optional history cannot abandon a visible bundle.
        for(size_t i=0;i<material_count;++i) {
            const vrm::Material fallback{};const auto& source=i<assets->asset->materials.size()?assets->asset->materials[i]:fallback;NativeMaterial material{};
            std::copy(source.base_color.begin(),source.base_color.end(),material.base);material.surface[1]=.35f;material.surface[3]=1;
            material.instance[0]=latest->parts[0][3];material.instance[3]=1;material.alpha[0]=source.alpha_cutoff;material.alpha[1]=source.alpha==vrm::AlphaMode::Mask?1.f:source.alpha==vrm::AlphaMode::Blend?2.f:0.f;
            if(bundle->menu_cutout&&source.alpha==vrm::AlphaMode::Blend){material.alpha[0]=.5f;material.alpha[1]=1.f;}
            material.alpha[2]=source.double_sided?1.f:0.f;
            material.alpha[3]=probe_mode;
            std::memcpy(static_cast<uint8_t*>(bundle->materials->mapped)+stride*i,&material,sizeof material);
        }
        const auto count=static_cast<uint32_t>(material_count);
        const std::array<VkDescriptorPoolSize,4> sizes={{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,65+2*count+(replacement?1u:0u)},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,6+2*count+(replacement?1u:0u)},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count},{VK_DESCRIPTOR_TYPE_SAMPLER,count}}};
        VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=count+1+(replacement?1u:0u);pool.poolSizeCount=sizes.size();pool.pPoolSizes=sizes.data();checked(F(vkCreateDescriptorPool)(context.device,&pool,nullptr,&bundle->pool),"Native bundle pool");
        std::vector<VkDescriptorSetLayout> set_layouts(count+1,draw_set);set_layouts[0]=prepare_set;std::vector<VkDescriptorSet> sets(count+1);
        if(replacement){set_layouts.push_back(clips->private_layout());sets.resize(set_layouts.size());}
        VkDescriptorSetAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocation.descriptorPool=bundle->pool;allocation.descriptorSetCount=sets.size();allocation.pSetLayouts=set_layouts.data();checked(F(vkAllocateDescriptorSets)(context.device,&allocation,sets.data()),"Native bundle sets");bundle->prepare=sets[0];
        std::array<VkDescriptorBufferInfo,71> buffers{};buffers[0]={bundle->control->handle,0,sizeof(NativePrepareControl)};buffers[1]=bundle->instances.info;buffers[2]=bundle->culled.info;buffers[3]=bundle->indirect.info;buffers[4]=bundle->counts.info;buffers[5]={bundle->frame->handle,0,bundle->frame->size};
        for(uint32_t i=0;i<64;++i)buffers[6+i]={bundle->arena.info.buffer,i*65536ull<bundle->arena.size?i*65536ull:0,65536};
        // When history is absent this valid alias is dynamically unused. The
        // shader reads it only under the final CPU history permit.
        buffers[70]={bundle->previous?bundle->previous->frame->handle:bundle->frame->handle,0,sizeof(NativePreparedFrame)};
        std::array<VkWriteDescriptorSet,71> writes{};
        for(uint32_t i=0;i<writes.size();++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=bundle->prepare;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i==0||(i>=6&&i<70)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&buffers[i];}
        // The caller holds Impl::mutex from the final backing/generation check
        // through this write. Native destroy/free hold that same mutex through
        // their driver call, so a stale handle cannot enter a new descriptor.
        if(!sources_current(bundle->sources)){report("source backing changed before descriptor construction");return{};}
        F(vkUpdateDescriptorSets)(context.device,writes.size(),writes.data(),0,nullptr);
        if(replacement) {
            bundle->body_bits=make_buffer(context,sizeof(bundle->body_mask.words),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            std::memcpy(bundle->body_bits->mapped,bundle->body_mask.words.data(),sizeof(bundle->body_mask.words));bundle->clip_set=sets.back();
            const std::array<VkDescriptorBufferInfo,2> clip_buffers={{{bundle->frame->handle,0,sizeof(NativePreparedFrame)},
                {bundle->body_bits->handle,0,sizeof(bundle->body_mask.words)}}};
            std::array<VkWriteDescriptorSet,2> clip_writes{};
            for(uint32_t i=0;i<2;++i){auto& w=clip_writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=bundle->clip_set;
                w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&clip_buffers[i];}
            F(vkUpdateDescriptorSets)(context.device,clip_writes.size(),clip_writes.data(),0,nullptr);
        }
        for(uint32_t i=0;i<count;++i) {
            const std::array<VkDescriptorBufferInfo,4> buf={{{bundle->frame->handle,0,sizeof(NativePreparedFrame)},{bundle->materials->handle,stride*i,sizeof(NativeMaterial)},vertex_binding(*bundle),index_binding(*bundle)}};
            const std::array<VkDescriptorImageInfo,2> images={{{VK_NULL_HANDLE,assets->images[assets->image_for_material[i]]->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{assets->samplers[i],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}}};
            std::array<VkWriteDescriptorSet,6> draw_writes{};
            for(uint32_t j=0;j<6;++j){auto& w=draw_writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[i+1];w.dstBinding=j;w.descriptorCount=1;w.descriptorType=j==0||j==3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:j==1?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:j==2?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;if(j==1||j==2)w.pImageInfo=&images[j-1];else w.pBufferInfo=&buf[j?j-2:0];}
            F(vkUpdateDescriptorSets)(context.device,draw_writes.size(),draw_writes.data(),0,nullptr);
        }
        for(const auto index:bundle->material_plan.opaque_draws){const auto& draw=latest->geometry->draws[index];const auto material=draw.material<0?count-1:static_cast<uint32_t>(draw.material);if(material>=count)return{};bundle->calls.push_back({sets[material+1],draw.first_index,draw.index_count});}
        if(artifact_probe_file.empty()||probe_mode==5.f)for(const auto& draw:latest->geometry->draws){const auto material=draw.material<0?count-1:static_cast<uint32_t>(draw.material);if(material>=count)return{};bundle->shadow_calls.push_back({sets[material+1],draw.first_index,draw.index_count});}
        const auto& qualified=replacement?bundle->qualified_parts:latest->parts;
        bundle->data.permit[1]=static_cast<uint32_t>(qualified.size());bundle->data.permit[2]=static_cast<uint32_t>(bundle->instances.info.range/128);
        bundle->data.offsets[0]=static_cast<uint32_t>(bundle->vis_camera.info.offset);bundle->data.offsets[3]=static_cast<uint32_t>(bundle->arena.size);
        bundle->data.extent[0]=recording.pass.extent.width;bundle->data.extent[1]=recording.pass.extent.height;
        bundle->data.config[0]=latest->height_scale;bundle->data.config[1]=latest->basis_xz;bundle->data.config[2]=replacement?0:latest->diagnostic_x;
        if(bundle->material_plan.mixed){bundle->data.blend[0]=1;std::copy(bundle->material_plan.blend_min.begin(),bundle->material_plan.blend_min.end(),bundle->data.blend_min);std::copy(bundle->material_plan.blend_max.begin(),bundle->material_plan.blend_max.end(),bundle->data.blend_max);std::copy(bundle->material_plan.base_max.begin(),bundle->material_plan.base_max.end(),bundle->data.blend_base_max);std::copy(bundle->material_plan.emissive_max.begin(),bundle->material_plan.emissive_max.end(),bundle->data.blend_emissive_max);}
        for(size_t i=0;i<qualified.size();++i)std::copy(qualified[i].begin(),qualified[i].end(),bundle->data.parts[i]);
        resource_timing(1,resources_started);resource_timing(0,bundle_started);
        preparation.complete=true;
        return bundle;
    }
    void recover_submission(VkQueue submitted_queue) {
        const auto idle=F(vkQueueWaitIdle)(submitted_queue);
        if(idle!=VK_SUCCESS&&idle!=VK_ERROR_DEVICE_LOST){context.uncertain=true;if(clips)clips->quarantine();return;}
        if(idle==VK_SUCCESS)each_recording([&](VkCommandBuffer command,Recording& item){if(item.bundle&&item.bundle->submitted_queue==submitted_queue)harvest_mixed(command,item,"queue-idle-success");});
        each_recording([&](VkCommandBuffer,Recording& item){if(item.bundle) {
            auto& b=*item.bundle;
            if(b.failed_submit&&b.submitted_queue==submitted_queue){b.data.permit[0]=0;std::memcpy(b.control->mapped,&b.data,sizeof b.data);b.retry_safe=true;}
        }});
    }
    VkResult retry_gate(VkQueue attempted_queue,VkCommandBuffer command) {
        VkResult result=VK_SUCCESS;
        // The caller holds this queue's external synchronization lock. Never
        // wait a different queue under the wrong lock.
        each_view(command,[&](VkCommandBuffer,Recording& item){if(item.bundle){auto& b=*item.bundle;
            if(b.failed_submit&&!b.retry_safe){if(b.submitted_queue==attempted_queue)recover_submission(attempted_queue);if(!b.retry_safe)result=last_submit_error;}
        }});return result;
    }
    void submission_decline_diagnostic(VkCommandBuffer command,const Recording& r,const Bundle& b,bool shape) noexcept {
        // Bounded CPU metadata only. Never read prepared GPU output, renew a
        // lifetime, or issue a driver command to explain an existing decline.
        if(submission_decline_samples>=16)return;
        const auto count=++submission_declines;
        if(count>4&&count%300)return;
        ++submission_decline_samples;
        try {
            const auto now=GetTickCount64();
            const bool age=now>=b.pose->published_milliseconds&&now-b.pose->published_milliseconds<=250;
            bool shadows=true,excluded=true,colors=true;unsigned competing=0;std::string sibling_facts;
            for(const auto& v:b.shadow_views)shadows=shadows&&same_image(v.depth,metadata.attachment_view(v.depth.view));
            for(const auto& v:b.oit_colors)colors=colors&&same_image(v,metadata.attachment_view(v.view));
            for(const auto& s:b.excluded_transforms)excluded=excluded&&metadata.live(s)&&
                memory.disjoint(b.instances.info.buffer,0,b.instances.size,s.info.buffer,s.info.offset,s.info.range);
            each_recording([&](VkCommandBuffer native,const Recording& other){if(&other!=&r&&other.affected_pass&&
                (native==command||other.ownership_window==b.pose->serial||(b.pose->presentation_serial&&
                    other.ownership_presentation==b.pose->presentation_serial))){
                ++competing;if(competing>4)return;
                sibling_facts+=" sibling={command="+std::to_string(reinterpret_cast<std::uintptr_t>(native))+
                    " same-primary="+std::to_string(native==command)+" unknown="+std::to_string(other.ownership_unknown)+
                    " unknown-reason="+(other.ownership_unknown_reason?other.ownership_unknown_reason:"none")+
                    " closed="+std::to_string(other.ownership_closed)+" independent="+std::to_string(independent_view(other,b))+
                    " sources="+std::to_string(other.ownership_sources.size());
                for(const auto& source:other.ownership_sources)sibling_facts+=" ["+
                    std::to_string(reinterpret_cast<std::uintptr_t>(source.info.buffer))+","+std::to_string(source.size)+
                    ",live="+std::to_string(metadata.live(source))+",disjoint="+
                    std::to_string(memory.disjoint(b.instances.info.buffer,0,b.instances.size,source.info.buffer,0,source.size))+"]";
                sibling_facts+="}";
            }});
            std::string populate;
            if(b.material_plan.mixed){
                populate=" populate-attempted="+std::to_string(b.snapshot_attempted)+" populate-recorded="+std::to_string(b.snapshot_recorded)+
                    " populate-decline="+b.snapshot_decline+" populate-groups="+std::to_string(b.snapshot_dispatch[0])+","+
                    std::to_string(b.snapshot_dispatch[1])+","+std::to_string(b.snapshot_dispatch[2])+" populate-extent="+
                    std::to_string(b.snapshot_extent.width)+","+std::to_string(b.snapshot_extent.height)+","+std::to_string(b.snapshot_extent.depth);
                if(b.snapshot_decline_image&&image_memory){const auto facts=image_memory->diagnostic(b.snapshot_decline_image);
                    populate+=" populate-image-known="+std::to_string(facts.image_known)+" populate-target="+std::to_string(facts.target)+
                        " populate-query-safe="+std::to_string(facts.query_safe)+" populate-bound="+std::to_string(facts.bind_attempted)+
                        " populate-footprint="+std::to_string(facts.footprint_known)+" populate-allocation-known="+std::to_string(facts.allocation_known)+
                        " populate-ordinary="+std::to_string(facts.allocation_ordinary)+" populate-uncertain="+std::to_string(facts.allocation_uncertain)+
                        " populate-registry-disabled="+std::to_string(facts.disabled)+" populate-memory-properties="+std::to_string(facts.memory_properties)+
                        " populate-memory-generations="+std::to_string(facts.bound_memory_generation)+","+std::to_string(facts.current_memory_generation);}
                if(b.snapshot_decline_binding!=UINT32_MAX)populate+=" populate-cell={"+
                    metadata.sampled_binding_diagnostic(b.snapshot_decline_bound,2,b.snapshot_decline_binding)+"}";
            }
            log_event("native-replacement.submission-declined shape="+std::to_string(shape)+" eligible="+std::to_string(r.eligible)+
                " tainted="+std::to_string(r.tainted)+" invalid="+std::to_string(b.invalid)+" recorded="+std::to_string(b.recorded)+" ended="+std::to_string(b.ended)+
                " visibility="+std::to_string(b.visibility_passes)+"/"+std::to_string(b.visibility_draws)+" gbuffer="+std::to_string(b.gbuffer_passes)+"/"+std::to_string(b.gbuffer_draws)+
                " arena="+std::to_string(b.gbuf_camera&&b.owner&&b.gbuf_camera.info.buffer==b.arena.info.buffer&&b.owner.info.buffer==b.arena.info.buffer&&
                    b.gbuf_camera.generation==b.arena.generation&&b.owner.generation==b.arena.generation)+
                " sources="+std::to_string(sources_current(b.sources)&&source_current(b.gbuf_camera)&&source_current(b.owner))+" shadows="+std::to_string(shadows)+
                " mixed="+std::to_string(b.material_plan.mixed)+" volume="+std::to_string(b.material_plan.mixed&&mixed_volume_current(b))+
                " late="+std::to_string(b.material_plan.mixed&&mixed_late_current(b))+" raster="+std::to_string(b.oit_raster.matches(b.first_pass.extent))+
                " depth="+std::to_string(same_image(b.oit_depth,metadata.attachment_view(b.oit_depth.view)))+" colors="+std::to_string(colors)+
                " window="+std::to_string(b.oit_window.candidates)+","+std::to_string(b.oit_window.insertions)+","+std::to_string(b.oit_window.active)+","+std::to_string(b.oit_window.lit)+","+std::to_string(b.oit_window.failed)+
                " clips="+std::to_string(b.visibility_clips)+"/"+std::to_string(b.gbuffer_clips)+" mask="+std::to_string(bool(b.clip_set)&&bool(b.body_bits))+
                " memory="+std::to_string(memory.valid(b.instances.info.buffer,0,b.instances.size))+" affected="+std::to_string(r.affected_pass)+
                " window-current="+std::to_string(r.ownership_window==b.pose->serial)+" competing="+std::to_string(competing)+" excluded="+std::to_string(excluded)+
                " latest="+std::to_string(latest&&latest->asset==b.pose->asset)+" fresh="+std::to_string(age)+" age-ms="+std::to_string(now>=b.pose->published_milliseconds?now-b.pose->published_milliseconds:UINT64_MAX)+
                " parts="+std::to_string(b.data.permit[3])+" source="+std::to_string(reinterpret_cast<std::uintptr_t>(b.instances.info.buffer))+
                " extent="+std::to_string(b.first_pass.extent.width)+"x"+std::to_string(b.first_pass.extent.height)+
                " menu="+std::to_string(b.menu_preview)+" owner-kind="+std::to_string(static_cast<unsigned>(b.menu_owner.kind))+sibling_facts+populate);
        }catch(...){}
    }
    void submit_recording(VkCommandBuffer command,Recording& recording,bool shape,VkQueue submitted_queue) {
        if(!recording.bundle)return;
        auto& b=*recording.bundle;
        const bool character_preview=b.menu_preview&&b.menu_owner.kind==NativeMenuOwnerKind::character;
        const auto latest=publication_for(b.instances.info.buffer);
        if(b.submitted)return;
        b.submitted=true;b.awaiting_submit_result=true;b.submitted_queue=submitted_queue;
        bool ready=shape&&recording.eligible&&!recording.tainted&&!b.invalid&&b.recorded&&b.ended&&b.visibility_passes>=1&&b.visibility_passes<=2&&b.gbuffer_passes==1&&
            b.visibility_passes==b.visibility_draws&&b.gbuffer_draws==1&&b.gbuf_camera&&b.owner&&b.data.permit[3]>0&&
            b.gbuf_camera.info.buffer==b.arena.info.buffer&&b.owner.info.buffer==b.arena.info.buffer&&
            b.gbuf_camera.generation==b.arena.generation&&b.owner.generation==b.arena.generation&&
            b.gbuf_camera.info.offset<=UINT32_MAX&&b.owner.info.offset<=UINT32_MAX&&latest&&latest->asset==b.pose->asset&&
            GetTickCount64()>=b.pose->published_milliseconds&&GetTickCount64()-b.pose->published_milliseconds<=250;
        ready=ready&&sources_current(b.sources)&&source_current(b.gbuf_camera)&&source_current(b.owner);
        for(const auto& view:b.shadow_views){const auto current=metadata.attachment_view(view.depth.view);ready=ready&&current.current&&
            current.image==view.depth.image&&current.image_generation==view.depth.image_generation&&current.view_generation==view.depth.view_generation;}
        if(b.material_plan.mixed)ready=ready&&mixed_current(b)&&b.oit_window.complete();
        if(replacement) {
            ready=ready&&b.visibility_clips>0&&b.gbuffer_clips>0&&b.clip_set&&b.body_bits&&
                memory.valid(b.instances.info.buffer,0,b.instances.size)&&recording.affected_pass&&recording.ownership_window==b.pose->serial;
            // Veto other affected primaries already observed in this publication
            // window, including earlier separate submissions. This observation
            // does not predict future command recording after this submission.
            each_recording([&](VkCommandBuffer native,const Recording& other){if(&other!=&recording&&other.affected_pass&&
                (native==command||other.ownership_window==b.pose->serial||(b.pose->presentation_serial&&
                    other.ownership_presentation==b.pose->presentation_serial))&&!independent_view(other,b))ready=false;});
            for(const auto& source:b.excluded_transforms)ready=ready&&metadata.live(source)&&
                memory.disjoint(b.instances.info.buffer,0,b.instances.size,source.info.buffer,source.info.offset,source.info.range);
            std::string reason;
            NativeOwnerObservationStats owner_stats;bool owner_failed=false;
            if(ready){
                if(b.menu_preview){ready=revalidate_menu_owner(b.menu_owner,reason);}
                else{const auto owner_started=GetTickCount64();ready=revalidate_owner(b.cpu_owner,reason,&owner_stats);
                    if(!ready)ready=refresh_census_owner(b,owner_started,reason,owner_stats);}
                owner_failed=!ready;
            }
            if(ready){NativeBodyMask current;
                if(b.menu_preview){std::vector<std::array<std::uint32_t,4>> qualified;
                    ready=make_native_menu_body_mask(b.menu_owner,b.pose->parts,b.pose->instance_qualifiers,b.instances,current,qualified,reason)&&
                        qualified==b.qualified_parts&&current.words==b.body_mask.words;
                }else ready=make_native_body_mask(b.cpu_owner,b.pose->parts,b.instances,current,reason)&&current.words==b.body_mask.words;
            }
            if(!ready&&!reason.empty()){
                report("replacement submission owner declined: "+reason);
                if(owner_failed)owner_diagnostic("submission",owner_stats);
            }
        }
        b.data.offsets[1]=static_cast<uint32_t>(b.gbuf_camera.info.offset);b.data.offsets[2]=static_cast<uint32_t>(b.owner.info.offset);b.data.permit[0]=ready?1:0;
        if(replacement){
            if(b.menu_preview){if(!character_preview)tool_slots.disable();}
            else if(ready&&owner_process){const auto started=std::chrono::steady_clock::now();tool_slots.publish(*owner_process,b.cpu_owner,b.pose,b.instances.generation);resource_timing(3,started);}else if(!ready)revoke_preparation(b.instances);
            // Initial hook installation may take time. It cannot extend the
            // original pose age accepted by either CPU or tool authorization.
            ready=ready&&GetTickCount64()>=b.pose->published_milliseconds&&GetTickCount64()-b.pose->published_milliseconds<=250;
            if(!ready)revoke_preparation(b.instances);
            if(ready)retain_owner_hint(b);
            // Optional first-seed allocation/copy is bounded in size, not time.
            // It must not carry an otherwise fresh pose across the age limit.
            ready=ready&&GetTickCount64()>=b.pose->published_milliseconds&&GetTickCount64()-b.pose->published_milliseconds<=250;
            if(!ready)revoke_preparation(b.instances);
            b.data.permit[0]=ready?1:0;
        }
        b.data.history[0]=ready&&b.previous&&b.previous==history_for(b.instances.info.buffer,b.first_pass.extent)&&latest&&
            latest->serial==b.pose->serial&&same_selection(*b.previous,b)?1u:0u;
        std::memcpy(b.control->mapped,&b.data,sizeof b.data);
        if(replacement){if(ready)++coverage_accepted;else ++coverage_declined;if((ready&&coverage_accepted==1)||(coverage_accepted+coverage_declined)%600==0)coverage_report();}
        try {
            if(ready){++permits;if(permits==1||permits%300==0)log_event(std::string(replacement?"native-replacement.cpu-permit count=":"native-additive.cpu-permit count=")+std::to_string(permits)+" pose-serial="+std::to_string(b.pose->serial)+" pose-age-ms="+std::to_string(GetTickCount64()-b.pose->published_milliseconds)+(b.data.history[0]?" history=requested original-body=":" history=invalid original-body=")+(replacement?"shared-gpu-clip":"retained"));last_reason.clear();}
            else {submission_decline_diagnostic(command,recording,b,shape);report("paired pass/source/pose submission gate declined");}
        }catch(...){}
    }
    void submit_command(VkCommandBuffer command,bool shape,VkQueue submitted_queue){
        each_view(command,[&](VkCommandBuffer native,Recording& recording){submit_recording(native,recording,shape,submitted_queue);});
    }
    void finish_history_submission(VkQueue submitted_queue) {
        std::map<HistoryKey,unsigned> candidates;
        each_recording([&](VkCommandBuffer,const Recording& item){if(item.bundle){const auto& b=*item.bundle;
            if(b.awaiting_submit_result&&b.submitted_queue==submitted_queue&&b.data.permit[0])++candidates[history_key(b.instances.info.buffer,b.first_pass.extent)];}});
        for(const auto& pair:candidates)if(pair.second>1)update_history(pair.first,{});
        each_recording([&](VkCommandBuffer,Recording& item){if(item.bundle){auto& b=*item.bundle;
            if(b.awaiting_submit_result&&b.submitted_queue==submitted_queue&&b.data.history[0]&&
                (candidates[history_key(b.instances.info.buffer,b.first_pass.extent)]>1||b.previous!=history_for(b.instances.info.buffer,b.first_pass.extent))){
                b.data.history[0]=0;
                std::memcpy(static_cast<unsigned char*>(b.control->mapped)+offsetof(NativePrepareControl,history),b.data.history,sizeof b.data.history);
            }}});
    }

    ~Impl() {
        coverage_report();
        tool_slots.clear();
        const auto idle=context.device?F(vkDeviceWaitIdle)(context.device):VK_SUCCESS;
        if(context.device&&idle==VK_SUCCESS)each_recording([&](VkCommandBuffer command,Recording& item){harvest_mixed(command,item,"device-idle-success");});
        if(idle!=VK_SUCCESS&&idle!=VK_ERROR_DEVICE_LOST)context.uncertain=true;else context.uncertain=false;
        if(clips&&context.uncertain)clips->quarantine();
        discard_history();completed_views.clear();commands.clear();latest.reset();assets.reset();layouts.clear();
        clips.reset();
        if(context.uncertain){draw_pipelines.release();oit_pipelines.release();shadow_pipeline.release();return;}
        shadow_pipeline.reset();for(auto s:shadow_shaders)if(s)F(vkDestroyShaderModule)(context.device,s,nullptr);
        if(shadow_layout)F(vkDestroyPipelineLayout)(context.device,shadow_layout,nullptr);
        if(shadow_set)F(vkDestroyDescriptorSetLayout)(context.device,shadow_set,nullptr);
        oit_pipelines.reset();if(volume_sampler)F(vkDestroySampler)(context.device,volume_sampler,nullptr);if(oit_shader)F(vkDestroyShaderModule)(context.device,oit_shader,nullptr);if(oit_layout)F(vkDestroyPipelineLayout)(context.device,oit_layout,nullptr);if(oit_set)F(vkDestroyDescriptorSetLayout)(context.device,oit_set,nullptr);
        draw_pipelines.reset();if(prepare_pipeline)F(vkDestroyPipeline)(context.device,prepare_pipeline,nullptr);
        if(menu_prepare_pipeline)F(vkDestroyPipeline)(context.device,menu_prepare_pipeline,nullptr);
        if(menu_prepare_shader)F(vkDestroyShaderModule)(context.device,menu_prepare_shader,nullptr);
        for(auto shader:shaders)if(shader)F(vkDestroyShaderModule)(context.device,shader,nullptr);
        if(draw_layout)F(vkDestroyPipelineLayout)(context.device,draw_layout,nullptr);
        if(prepare_layout)F(vkDestroyPipelineLayout)(context.device,prepare_layout,nullptr);
        if(draw_set)F(vkDestroyDescriptorSetLayout)(context.device,draw_set,nullptr);
        if(prepare_set)F(vkDestroyDescriptorSetLayout)(context.device,prepare_set,nullptr);
        if(upload_fence)F(vkDestroyFence)(context.device,upload_fence,nullptr);
        if(upload_pool)F(vkDestroyCommandPool)(context.device,upload_pool,nullptr);
    }
#undef F
    void coverage_report()noexcept {
        if(!replacement)return;
        try{log_event("native-replacement.coverage model-draws="+std::to_string(coverage_models)+" independent-draws="+std::to_string(coverage_independent)+
            " uncovered-declines="+std::to_string(coverage_uncovered)+" source-declines="+std::to_string(coverage_sources)+
            " common-accepted="+std::to_string(coverage_accepted)+" common-declined="+std::to_string(coverage_declined));}catch(...){}
    }
    void initialize_shadow(){
        if(shadow_pipeline)return;
        if(shadow_attempted)throw std::runtime_error("Native shadow resources unavailable");
        shadow_attempted=true;
        const VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo set{};set.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;set.bindingCount=1;set.pBindings=&binding;
        checked(function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device,&set,nullptr,&shadow_set),"Native shadow view set");
        const std::array<VkDescriptorSetLayout,2> sets{{draw_set,shadow_set}};
        VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=sets.size();layout.pSetLayouts=sets.data();
        checked(function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(context.device,&layout,nullptr,&shadow_layout),"Native shadow layout");
        shadow_shaders[0]=shader("avatar_native.shadow.vert.spv");shadow_shaders[1]=shader("avatar_native.shadow.frag.spv");
        shadow_pipeline=std::make_unique<NativeShadowDrawPipeline>(context.device,context.get,shadow_layout,shadow_shaders[0],shadow_shaders[1]);
    }
    bool shadow_raster_current(const Recording& r,const NativeRasterState& raster)const noexcept{
        const auto& v=raster.viewport;const auto& s=raster.scissor;const auto& d=r.shadow_depth;
        return d.current&&d.format==VK_FORMAT_D32_SFLOAT&&d.type==VK_IMAGE_VIEW_TYPE_2D&&d.samples==VK_SAMPLE_COUNT_1_BIT&&
            (d.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)&&d.subresource.levelCount==1&&d.subresource.layerCount==1&&
            raster.viewport_known&&raster.scissor_known&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.width)&&std::isfinite(v.height)&&
            v.x>=0&&v.y>=0&&v.width>0&&v.height>0&&v.x+v.width<=d.extent.width&&v.y+v.height<=d.extent.height&&v.minDepth==0&&v.maxDepth==1&&
            s.offset.x>=0&&s.offset.y>=0&&s.extent.width&&s.extent.height&&
            std::uint64_t(s.offset.x)+s.extent.width<=d.extent.width&&std::uint64_t(s.offset.y)+s.extent.height<=d.extent.height;
    }
    void caster_shadow_draw(Recording& r,VkCommandBuffer command){
        if(!r.pass_active||!r.shadow_pass||!r.bundle||r.native_restore||context.uncertain)return;
        auto& b=*r.bundle;if((!artifact_probe_file.empty()&&b.probe_mode!=5.f)||b.invalid||!b.clip_set||!b.body_bits||!clips)return;
        const auto state=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        if(!state.valid||state.pipeline.graphics_stage_count!=1)return;
        const auto instances=metadata.observed_buffer_binding(state,2,4);
        if(!same_binding(instances,b.instances)||instances.size!=b.instances.size)return;
        const auto pipeline=clips->pipeline(state.pipeline.pipeline);
        const auto source=metadata.observed_buffer_binding(state,1,4),culled=metadata.observed_buffer_binding(state,2,5);
        const auto raster=metadata.raster(command);const auto restore=compatible(state);
        if(!state.pipeline.shadow_compatible||!pipeline||pipeline->kind!=NativeClipKind::shadow||pipeline->mesh||!restore||
           !source_current(instances)||!memory.valid(instances.info.buffer,0,instances.size)||
           !culled||!(culled.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||culled.info.range==VK_WHOLE_SIZE||culled.info.range%8||!source_current(culled)||
           !source||!(source.usage&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)||!source_current(source)||
           source.info.buffer!=b.arena.info.buffer||source.generation!=b.arena.generation||source.size!=b.arena.size||source.info.range==VK_WHOLE_SIZE||source.info.range<352||
           source.info.offset>UINT32_MAX||source.info.offset>source.size||source.info.range>source.size-source.info.offset||
           !limits.minUniformBufferOffsetAlignment||source.info.offset%limits.minUniformBufferOffsetAlignment||!shadow_raster_current(r,raster)||
           b.shadow_probe_draws>=128||b.sources.size()>2044){b.invalid=true;
            if(shadow_contract_notices++<4)try{std::ostringstream out;out<<"native-shadow.contract-declined trait="<<state.pipeline.shadow_compatible<<" clip="<<bool(pipeline)<<" restore="<<bool(restore)
                <<" source="<<bool(source)<<","<<source_current(source)<<","<<source.info.buffer<<","<<source.info.offset<<","<<source.info.range<<","<<source.size<<","<<source.generation
                <<" arena="<<b.arena.info.buffer<<","<<b.arena.size<<","<<b.arena.generation<<" alignment="<<limits.minUniformBufferOffsetAlignment
                <<" culled="<<bool(culled)<<","<<source_current(culled)<<","<<culled.info.range<<","<<culled.usage<<" instances="<<source_current(instances)<<","<<memory.valid(instances.info.buffer,0,instances.size)
                <<" raster="<<shadow_raster_current(r,raster)<<","<<raster.viewport_known<<","<<raster.scissor_known<<" vp="<<raster.viewport.x<<","<<raster.viewport.y<<","<<raster.viewport.width<<","<<raster.viewport.height<<","<<raster.viewport.minDepth<<","<<raster.viewport.maxDepth
                <<" sc="<<raster.scissor.offset.x<<","<<raster.scissor.offset.y<<","<<raster.scissor.extent.width<<","<<raster.scissor.extent.height
                <<" depth="<<r.shadow_depth.current<<","<<r.shadow_depth.format<<","<<r.shadow_depth.type<<","<<r.shadow_depth.samples<<","<<r.shadow_depth.usage<<","<<r.shadow_depth.extent.width<<","<<r.shadow_depth.extent.height<<","<<r.shadow_depth.subresource.levelCount<<","<<r.shadow_depth.subresource.layerCount;log_event(out.str());}catch(...){}
            return;}
        bool emitted=false;
        for(std::size_t i=r.shadow_view_start;i<b.shadow_views.size();++i){const auto& v=b.shadow_views[i];
            if(same_binding(v.source,source)&&v.depth.view==r.shadow_depth.view&&
               std::memcmp(&v.raster.viewport,&raster.viewport,sizeof raster.viewport)==0&&std::memcmp(&v.raster.scissor,&raster.scissor,sizeof raster.scissor)==0&&
               std::memcmp(&v.area,&r.shadow_area,sizeof r.shadow_area)==0){emitted=true;break;}}
        if(!emitted){
            if(b.shadow_views.size()>=32||b.shadow_calls.empty()){b.invalid=true;return;}
            initialize_shadow();
            if(!b.shadow_pool){const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,32};VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=32;pool.poolSizeCount=1;pool.pPoolSizes=&size;
                checked(function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device,&pool,nullptr,&b.shadow_pool),"Native shadow pool");}
            Bundle::ShadowView view;view.source=source;view.depth=r.shadow_depth;view.raster=raster;view.area=r.shadow_area;
            VkDescriptorSetAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocation.descriptorPool=b.shadow_pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&shadow_set;
            checked(function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device,&allocation,&view.set),"Native shadow view descriptor");
            const VkDescriptorBufferInfo buffer{source.info.buffer,source.info.offset,352};VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=view.set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffer;
            // The source/backing guard stays held until this private immutable set is written.
            function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device,1,&write,0,nullptr);
            b.graphics_restore.push_back(restore);b.shadow_views.push_back(view);
            b.data.shadow[0]=1;b.data.shadow[1]=static_cast<std::uint32_t>(b.shadow_views.size());b.data.shadowViews[b.shadow_views.size()-1][0]=static_cast<std::uint32_t>(source.info.offset);
            if(!shadow_pipeline->record(command,view.set,b.shadow_calls.data(),static_cast<std::uint32_t>(b.shadow_calls.size()),state.pipeline.pipeline,restore->handle,static_cast<std::uint32_t>(state.sets.size()),state.sets.data())){b.invalid=true;return;}
            ++shadow_caster_views;if(shadow_caster_views==1||shadow_caster_views%300==0)try{log_event("native-shadow.caster-view count="+std::to_string(shadow_caster_views)+" loaded-draws="+std::to_string(b.shadow_calls.size())+" common-frame=1");}catch(...){}
        }
        // Geometry deduplication never discards a native descriptor certificate:
        // another set may name the same bytes and retire before submission.
        b.sources.push_back(source);b.sources.push_back(instances);b.sources.push_back(culled);b.clip_leases.push_back(pipeline);r.native_restore=state.pipeline.pipeline;
        function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);
        function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->layout,5,1,&b.clip_set,0,nullptr);
        ++b.shadow_probe_draws;++shadow_caster_draws;
    }
    void diagnostic_shadow_draw(Recording& r,VkCommandBuffer command) {
        if(artifact_probe_file.empty()||!r.pass_active||!r.shadow_pass||!r.bundle||r.native_restore||context.uncertain)return;
        auto& b=*r.bundle;
        if(b.probe_mode!=4.f||b.invalid||!b.clip_set||!b.body_bits||!clips||b.shadow_probe_draws>=128||b.sources.size()>2046)return;
        const auto state=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        if(!state.valid||state.pipeline.graphics_stage_count!=1)return;
        const auto pipeline=clips->pipeline(state.pipeline.pipeline);
        if(!pipeline||pipeline->kind!=NativeClipKind::shadow||pipeline->mesh||pipeline->original_layout!=state.pipeline.layout)return;
        const auto instances=metadata.observed_buffer_binding(state,2,4),culled=metadata.observed_buffer_binding(state,2,5);
        if(!same_binding(instances,b.instances)||instances.size!=b.instances.size||!source_current(instances)||
           !memory.valid(instances.info.buffer,0,instances.size)||!culled||!(culled.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||
           culled.info.range==VK_WHOLE_SIZE||culled.info.range%8||!source_current(culled))return;
        b.sources.push_back(instances);b.sources.push_back(culled);b.clip_leases.push_back(pipeline);
        r.native_restore=state.pipeline.pipeline;
        function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);
        function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->layout,5,1,&b.clip_set,0,nullptr);
        ++b.shadow_probe_draws;++shadow_probe_draws;
        if(shadow_probe_draws==1||shadow_probe_draws%300==0)try{log_event("native-shadow.probe-draw count="+std::to_string(shadow_probe_draws)+" common-frame=1 source-instance128=1");}catch(...){}
    }
    void cover_native_draw(Recording& r,VkCommandBuffer command) {
        if(!r.bundle||!r.pass_active||!r.pass.supported||r.tainted)return;
        auto& b=*r.bundle;if(b.invalid)return;
        const auto decline=[&](const char* why){++coverage_sources;r.tainted=true;r.ownership_unknown=true;b.invalid=true;notice(why);};
        if(r.native_restore||b.sources.size()>=2048||b.clip_leases.size()>=512||b.excluded_transforms.size()>=512){decline("replacement draw budget or restoration state declined");return;}
        const auto state=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        if(!state.valid){decline("replacement draw bindings unavailable");return;}
        const auto identity=body_identity(state.pipeline);
        const auto coverage=classify_native_body_coverage(identity);
        if(coverage.kind==NativeBodyCoverageKind::brick){++coverage_independent;return;}
        if(coverage.kind==NativeBodyCoverageKind::unknown){decline("replacement draw shader coverage unknown");return;}
        if(coverage.kind==NativeBodyCoverageKind::vfx){
            for(auto binding:{0u,2u}){const auto source=metadata.observed_buffer_binding(state,2,binding);
                if(!source||source.info.range<80||!(source.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||!source_current(source)||
                   !memory.disjoint(source.info.buffer,source.info.offset,source.info.range,b.instances.info.buffer,b.instances.info.offset,b.instances.info.range)){decline("replacement VFX source unavailable or aliases owner");return;}b.sources.push_back(source);}
            ++coverage_independent;return;
        }
        const auto binding=coverage.kind==NativeBodyCoverageKind::clip?coverage.instance_binding:coverage.transform_binding;
        const auto source=metadata.observed_buffer_binding(state,2,binding);
        if(!source){decline("replacement transform source unavailable");return;}
        if(coverage.kind==NativeBodyCoverageKind::separate_transform||coverage.kind==NativeBodyCoverageKind::chunklet) {
            // This explicit preview replaces only the qualified Instance128
            // representation. Exact compact32 draws keep their native state;
            // an alternate representation may remain visible.
            const auto minimum=coverage.kind==NativeBodyCoverageKind::chunklet?176u:32u;
            if(source.info.range<minimum||!(source.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||!source_current(source)||
               !memory.disjoint(source.info.buffer,source.info.offset,source.info.range,b.instances.info.buffer,b.instances.info.offset,b.instances.info.range)){decline("replacement independent transform source unavailable or aliases owner");return;}
            b.sources.push_back(source);++coverage_independent;return;
        }
        if(source.info.buffer!=b.instances.info.buffer) {
            decline("replacement Instance128 draw uses another source buffer");return;
        }
        const bool gbuffer=coverage.clip_kind==NativeClipKind::gbuffer||coverage.clip_kind==NativeClipKind::billboard_gbuffer;
        if(gbuffer!=(r.pass.kind==NativePipelineKind::gbuffer)||source.generation!=b.instances.generation||source.size!=b.instances.size||
            source.info.offset||source.info.range!=b.instances.info.range||!memory.valid(source.info.buffer,0,source.size)) {
            decline("replacement instance source origin or pass changed");return;
        }
        const auto culled=metadata.observed_buffer_binding(state,2,coverage.culled_binding);
        const auto pipeline=clips?clips->pipeline(state.pipeline.pipeline):nullptr;
        if(!culled||!(culled.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)||culled.info.range%8||!pipeline||
            pipeline->kind!=coverage.clip_kind||pipeline->mesh!=bool(state.pipeline.mesh_bytes)||pipeline->original_layout!=state.pipeline.layout||!b.clip_set) {
            decline("replacement clipping pipeline or culled source unavailable");
            clip_diagnostic(state,"native-draw",b.clip_set?"1":"0");
            return;
        }
        b.sources.push_back(source);b.sources.push_back(culled);b.clip_leases.push_back(pipeline);
        r.native_restore=state.pipeline.pipeline;
        function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->handle);
        function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->layout,5,1,&b.clip_set,0,nullptr);
        ++coverage_models;++b.clipped_draws;if(gbuffer)++b.gbuffer_clips;else++b.visibility_clips;
    }

};

NativeBackend::NativeBackend(VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr get_instance,PFN_vkGetDeviceProcAddr get,PFN_vkSetDeviceLoaderData loader,const std::filesystem::path& shaders,bool independent_blend,bool replacement,bool partially_bound,bool mesh_enabled):impl(std::make_unique<Impl>()) {
    impl->context.device=device;impl->context.get=get;impl->set_loader=loader;impl->shader_directory=shaders;
    try {
        wchar_t probe_path[32768]{};
        const auto length=GetEnvironmentVariableW(L"ENSHROUDED_VRM_ARTIFACT_PROBE_FILE",probe_path,32768);
        if(length>0&&length<32768)impl->artifact_probe_file=std::filesystem::path(probe_path);
    }catch(...){}
    impl->instance=instance;impl->physical=physical;impl->get_instance=get_instance;
    impl->independent_blend=independent_blend;impl->partially_bound=partially_bound;impl->mesh_enabled=mesh_enabled;
    const auto memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get_instance(instance,"vkGetPhysicalDeviceMemoryProperties"));memory(physical,&impl->context.memory);
    const auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_instance(instance,"vkGetPhysicalDeviceProperties"));VkPhysicalDeviceProperties value{};properties(physical,&value);impl->limits=value.limits;
    if(value.apiVersion<VK_API_VERSION_1_3)throw std::runtime_error("Native shaders require a Vulkan 1.3 physical device");
    impl->replacement=replacement;
    if(replacement||partially_bound)impl->image_memory=std::make_unique<NativeImageMemory>(impl->context.memory,impl->limits.bufferImageGranularity);
    try{impl->transparency_output_ready=impl->transparency_trace.set_output([](const std::string& line){log_event(line);});}catch(...){}
    if(replacement){impl->clips=std::make_unique<NativeClipPipelines>(device,get,impl->limits,mesh_enabled,true);impl->owner_process=std::make_unique<NativeOwnerProcess>();}
}
NativeBackend::~NativeBackend()=default;
std::unique_lock<std::mutex> NativeBackend::native_source_guard(bool try_only) {
    std::unique_lock<std::mutex> guard(impl->mutex,std::defer_lock);
    if(try_only)(void)guard.try_lock();else guard.lock();
    return guard;
}
NativeState& NativeBackend::state()noexcept{return impl->metadata;}
NativeClipPipelines* NativeBackend::clipping()noexcept{return impl->clips.get();}
NativeBufferMemory* NativeBackend::buffer_memory()noexcept{return impl->replacement||impl->partially_bound?&impl->memory:nullptr;}
NativeImageMemory* NativeBackend::image_memory()noexcept{return impl->image_memory.get();}
#ifdef EVRM_NATIVE_BACKEND_TESTING
void NativeBackend::test_owner(std::shared_ptr<const NativeBackendTestOwner> source)noexcept {try{std::lock_guard<std::mutex> lock(impl->mutex);impl->discard_owner_hint();impl->test_owner_source=std::move(source);}catch(...){} }
#endif
void NativeBackend::publish(VkQueue queue,uint32_t family,std::shared_ptr<const NativeAvatarPublication> publication,std::uint64_t source_buffer_epoch)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);impl->observe_image_disable();impl->latest=std::move(publication);if(!impl->latest||impl->disabled||impl->context.uncertain){impl->view_publications.reset();impl->revoke_preparation();return;}
        impl->view_publications=impl->latest;
        impl->latest_source={};
        if(impl->replacement) {
            const auto before=impl->metadata.buffer_epoch();
            const auto source=impl->metadata.buffer(reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(impl->latest->source_instances)));
            if(!source_buffer_epoch||source_buffer_epoch!=before||before!=impl->metadata.buffer_epoch()||!source||
                impl->latest->source_instance_offset||impl->latest->source_instance_range!=source.size) {
                impl->report("replacement readback source identity changed");impl->latest.reset();impl->view_publications.reset();impl->revoke_preparation();return;
            }
            for(const auto& view:impl->latest->additional_views){
                const auto current=view?native_publication_view(impl->view_publications,view->source_instances):nullptr;
                const auto child=view?impl->metadata.buffer(reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(view->source_instances))):NativeBufferBinding{};
                if(!current||current!=view||view->source_buffer_epoch!=impl->latest->source_buffer_epoch||view->source_instance_offset||
                    !child||view->source_instance_range!=child.size){impl->latest.reset();impl->view_publications.reset();impl->revoke_preparation();return;}
            }
            const auto hint=std::atomic_load(&impl->owner_preparation_hint);
            if(hint&&(!native_publication_view(impl->view_publications,hint->candidate.native_buffer)||
                !impl->hint_source_current(*hint,hint->source)))impl->revoke_preparation();
            impl->latest_source=source;
        }
        if(impl->queue&&(impl->queue!=queue||impl->family!=family)){impl->revoke_preparation();impl->disabled=true;impl->report("queue changed");return;}impl->queue=queue;impl->family=family;
        if(!impl->assets&&impl->latest->asset&&std::any_of(impl->latest->asset->materials.begin(),impl->latest->asset->materials.end(),[](const vrm::Material& m){return m.alpha==vrm::AlphaMode::Blend;})){
            NativeMaterialPlan plan;if(!publication_material_plan(*impl->latest,plan)){impl->revoke_preparation();impl->report("mixed publication material partition declined before upload");return;}
        }
        impl->initialize();if(!impl->assets)impl->assets=impl->upload(impl->latest->asset);
        try{if(impl->wants_notice("publication-ready"))impl->notice("publication-ready parts="+std::to_string(impl->latest->parts.size())+" vertices="+std::to_string(impl->latest->geometry?impl->latest->geometry->vertices.size():0),"publication-ready");}catch(...){}
        if(impl->assets->asset!=impl->latest->asset){impl->revoke_preparation();impl->disabled=true;impl->report("asset changed");return;}
        if(impl->replacement&&impl->owner_process){const auto started=std::chrono::steady_clock::now();
            const auto hint=std::atomic_load(&impl->owner_preparation_hint);
            const auto world=hint?native_publication_view(impl->view_publications,hint->candidate.native_buffer):impl->latest;
            if(world){const auto source=impl->metadata.buffer(reinterpret_cast<VkBuffer>(static_cast<std::uintptr_t>(world->source_instances)));
                impl->tool_slots.advance(*impl->owner_process,world,source.generation);}
            impl->resource_timing(4,started);}
    }catch(const std::exception& e){impl->revoke_preparation();impl->disabled=true;try{impl->report(e.what());}catch(...){}}catch(...){impl->revoke_preparation();impl->disabled=true;}
}
void NativeBackend::pipeline_layout_created(VkPipelineLayout layout,const VkPipelineLayoutCreateInfo& info)noexcept {
    impl->metadata.pipeline_layout_created(layout,info);
    if(impl->clips)impl->clips->pipeline_layout_created(layout,info);
    try{std::lock_guard<std::mutex> lock(impl->mutex);if(info.pNext||info.flags||info.setLayoutCount>32||info.pushConstantRangeCount>32||impl->layouts.size()>=4096)return;
        auto clone=std::make_shared<CompatibleLayout>(impl->context);checked(impl->function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(impl->context.device,&info,nullptr,&clone->handle),"Native compatible layout");impl->layouts[layout]=std::move(clone);
    }catch(...){}
}
void NativeBackend::pipeline_layout_destroyed(VkPipelineLayout layout)noexcept {impl->metadata.pipeline_layout_destroyed(layout);if(impl->clips)impl->clips->pipeline_layout_destroyed(layout);try{std::lock_guard<std::mutex> lock(impl->mutex);impl->layouts.erase(layout);}catch(...){}}
void NativeBackend::command_begin(VkCommandBuffer command,const VkCommandBufferBeginInfo& info)noexcept {
    impl->metadata.command_begin(command);try{std::lock_guard<std::mutex> lock(impl->mutex);
        bool retiring=false;impl->each_view(command,[&](VkCommandBuffer,const Recording& item){retiring=retiring||static_cast<bool>(item.bundle);});const auto started=std::chrono::steady_clock::now();
        impl->retire_views(command,"valid-original-rebegin");auto& r=impl->commands[command];
        if(retiring)impl->resource_timing(2,started);
        impl->transparency_trace.begin(r.transparency,command);r.eligible=!info.pNext&&info.flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }catch(...){}
}
void NativeBackend::allocated(const VkCommandBufferAllocateInfo& info,const VkCommandBuffer* commands)noexcept {try{std::lock_guard<std::mutex> lock(impl->mutex);if(!commands||info.commandBufferCount>512||impl->pools.size()+info.commandBufferCount>512)return;for(uint32_t i=0;i<info.commandBufferCount;++i)impl->pools[commands[i]]=info.commandPool;}catch(...){} }
void NativeBackend::command_freed(uint32_t count,const VkCommandBuffer* commands)noexcept {
    impl->metadata.command_freed(count,commands);try{std::lock_guard<std::mutex> lock(impl->mutex);
        for(uint32_t i=0;i<count;++i){impl->retire_views(commands[i],"valid-original-free");impl->pools.erase(commands[i]);}
    }catch(...){} }
void NativeBackend::command_reset(VkCommandBuffer command)noexcept {
    impl->metadata.command_freed(1,&command);try{std::lock_guard<std::mutex> lock(impl->mutex);impl->retire_views(command,"valid-original-reset");}catch(...){} }
void NativeBackend::pool_reset(VkCommandPool pool)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);for(const auto& item:impl->pools)if(item.second==pool){
        impl->metadata.command_freed(1,&item.first);impl->retire_views(item.first,"valid-original-pool-retirement");
    }}catch(...){} }
void NativeBackend::pool_destroyed(VkCommandPool pool)noexcept {pool_reset(pool);try{std::lock_guard<std::mutex> lock(impl->mutex);for(auto i=impl->pools.begin();i!=impl->pools.end();)if(i->second==pool)i=impl->pools.erase(i);else++i;}catch(...){} }
void NativeBackend::incoming_rendering(VkCommandBuffer command,const VkRenderingInfo& info)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;
        const auto pass=impl->metadata.rendering(info);impl->next_view(command,pass);r.pass=pass;r.pass_active=true;r.pass_drawn=false;r.first_indirect={};r.first_count={};
        r.shadow_pass=!info.pNext&&!info.flags&&info.layerCount==1&&!info.viewMask&&!info.colorAttachmentCount&&
            !info.pStencilAttachment&&info.pDepthAttachment&&!info.pDepthAttachment->pNext&&info.pDepthAttachment->imageView&&
            info.pDepthAttachment->resolveMode==VK_RESOLVE_MODE_NONE&&!info.pDepthAttachment->resolveImageView&&
            (info.pDepthAttachment->imageLayout==VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL||info.pDepthAttachment->imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        r.shadow_depth=r.shadow_pass?impl->metadata.attachment_view(info.pDepthAttachment->imageView):NativeOitView{};
        r.shadow_area=info.renderArea;r.shadow_view_start=r.bundle?r.bundle->shadow_views.size():0;
        impl->observe_rendering(r,info);
        impl->mixed_rendering(r,info);
        if(impl->replacement&&r.pass.supported) {
            const auto view=r.ownership_instances?impl->publication_for(r.ownership_instances.info.buffer):impl->view_publications;
            const auto window=view?view->serial:r.ownership_window;
            if(r.affected_pass&&r.ownership_window!=window){r.tainted=true;impl->unknown_ownership(r,"publication-window");}
            if(!r.affected_pass){r.affected_pass=true;r.ownership_window=window;r.ownership_presentation=view?view->presentation_serial:0;}
        }
        if(!r.pass.supported && (info.colorAttachmentCount==2 || info.colorAttachmentCount==6))impl->notice("unrecognized-pass colors="+std::to_string(info.colorAttachmentCount)+" depth-layout="+std::to_string(info.pDepthAttachment?info.pDepthAttachment->imageLayout:VK_IMAGE_LAYOUT_UNDEFINED));
        if(r.bundle){if(r.bundle->first_regions_open)r.bundle->first_regions_open=false;if(r.pass.kind==NativePipelineKind::visibility)++r.bundle->visibility_passes;else if(r.pass.kind==NativePipelineKind::gbuffer)++r.bundle->gbuffer_passes;}
    }catch(...){}
}
void NativeBackend::rendering_unavailable(VkCommandBuffer command)noexcept {scope_unsupported(command,"BeginRendering was not deferred");}
void NativeBackend::before_attachment_clear(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);
        if(found==impl->commands.end())return;
        auto& recording=found->second;
        // A depth clear can erase already emitted caster pixels. Begin a fresh
        // emission epoch without changing the common permit or native clear.
        if(recording.pass_active&&recording.shadow_pass&&recording.bundle)
            recording.shadow_view_start=recording.bundle->shadow_views.size();
    }catch(...){scope_unsupported(command,"shadow clear observation failed");}
}
void NativeBackend::before_rendering(VkCommandBuffer command,const VkRenderingInfo&,const char* release_trigger)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;
        if(!r.bundle && r.pass.supported && r.pass.kind==NativePipelineKind::visibility){const auto extent=std::to_string(r.pass.extent.width)+"x"+std::to_string(r.pass.extent.height);
            impl->notice("first-visibility-release extent="+extent+" trigger="+(release_trigger?release_trigger:"unspecified")+" eligible="+std::to_string(r.eligible)+" tainted="+std::to_string(r.tainted)+" producer="+std::to_string(r.compute_producer)+" counted="+std::to_string(static_cast<bool>(r.first_count)),"first-release-"+extent);}
        if(!r.eligible||r.tainted||r.bundle||!r.pass.supported||r.pass.kind!=NativePipelineKind::visibility||impl->disabled)return;
        if(!r.compute_producer||!r.first_count||!r.first_indirect){r.tainted=true;impl->unknown_ownership(r,"visibility-window");impl->notice("visibility-window-missed producer="+std::to_string(r.compute_producer)+" counted-draw="+std::to_string(static_cast<bool>(r.first_count)));return;}
        impl->clip_diagnostic(impl->metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS),"first-visibility","unobserved");
        auto b=impl->make_bundle(r,command);if(!b){r.tainted=true;impl->notice("current native preparation context unavailable","context-unavailable");return;}
        const auto compute=impl->metadata.bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);auto& context=impl->context;
        r.bundle=b;b->visibility_passes=1; // Retain before the first recorded private command.
        VkMemoryBarrier before{};before.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT;
        context.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,nullptr,0,nullptr);
        context.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,b->menu_preview?impl->menu_prepare_pipeline:impl->prepare_pipeline);
        context.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,impl->prepare_layout,0,1,&b->prepare,0,nullptr);
        context.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(command,1,1,1);
        if(b->skin)impl->skinning_gpu->record(command,b->skin);
        before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;before.dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_HOST_READ_BIT;
        context.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT|(impl->mesh_enabled?VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT:0),0,1,&before,0,nullptr,0,nullptr);
        context.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,compute.pipeline.pipeline);
        context.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,b->compute_restore->handle,0,compute.sets.size(),compute.sets.data(),0,nullptr);
        b->recorded=true;
    }catch(const std::exception& e){scope_unsupported(command);try{impl->report(e.what());}catch(...){}}catch(...){scope_unsupported(command);}
}
void NativeBackend::draw(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,VkBuffer counts,VkDeviceSize count_offset,uint32_t maximum,uint32_t stride)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;if(!r.pass_active||!r.pass.supported)return;
        const auto bound=impl->metadata.bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        if(r.pass.kind==NativePipelineKind::visibility){const auto extent=std::to_string(r.pass.extent.width)+"x"+std::to_string(r.pass.extent.height);
            const auto key="first-draw-"+extent;if(impl->wants_notice(key))impl->notice("first-visibility-counted-draw extent="+extent+" "+impl->metadata.binding_diagnostic(command,VK_PIPELINE_BIND_POINT_GRAPHICS),key);}
        if(!bound.valid||bound.pipeline.kind!=r.pass.kind){if(impl->replacement){
            // This callback selects a preparation witness. Every actual native
            // draw is audited below by before_native_draw, using its complete
            // observed shader identity and sources, including scene families
            // that are intentionally unsupported as body witnesses.
            const auto key="counted-state-"+std::to_string(r.pass.extent.width)+"x"+std::to_string(r.pass.extent.height)+"-"+std::to_string(static_cast<unsigned>(r.pass.kind));
            if(impl->wants_notice(key))impl->notice("counted-draw-state expected-kind="+std::to_string(static_cast<unsigned>(r.pass.kind))+
                " observed-kind="+std::to_string(static_cast<unsigned>(bound.pipeline.kind))+" "+impl->metadata.binding_diagnostic(command,VK_PIPELINE_BIND_POINT_GRAPHICS),key);
        }return;}
        if(impl->replacement)impl->observe_ownership_source(r,impl->metadata.buffer_binding(bound,2,4));
        if(!r.first_indirect){r.first_indirect=buffer;r.first_count=counts;r.first_offset=offset;r.first_count_offset=count_offset;r.first_max=maximum;r.first_stride=stride;}
        if(r.bundle&&r.pass.kind==NativePipelineKind::gbuffer){const auto camera=impl->metadata.buffer_binding(bound,1,16);if(r.bundle->gbuf_camera&&!same_binding(r.bundle->gbuf_camera,camera))r.bundle->invalid=true;else r.bundle->gbuf_camera=camera;}
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::render_current(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;if(!r.bundle||!r.pass_active||r.tainted)return;auto& b=*r.bundle;if(b.invalid)return;
        const auto bound=impl->metadata.bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);if(!bound.valid||bound.pipeline.kind!=r.pass.kind)return;
        if(r.pass.kind==NativePipelineKind::visibility) {
            const auto camera=impl->metadata.buffer_binding(bound,1,4);
            if(!same_binding(b.vis_camera,camera)){b.invalid=true;return;}
            b.sources.push_back(camera);
        }
        if(b.first_regions_open) {
            const auto instances=impl->metadata.buffer_binding(bound,2,4),culled=impl->metadata.buffer_binding(bound,2,5);
            if(!same_binding(b.instances,instances)||!same_binding(b.culled,culled)){b.invalid=true;return;}
            b.sources.push_back(instances);b.sources.push_back(culled);
            const auto i=b.data.permit[3];if(i>=32||r.first_indirect!=b.indirect.info.buffer||r.first_count!=b.counts.info.buffer||r.first_offset>UINT32_MAX||r.first_count_offset>UINT32_MAX||r.first_stride!=24||r.first_max>1024||!r.first_max)b.invalid=true;
            else{b.data.regions[i][0]=static_cast<uint32_t>(r.first_offset);b.data.regions[i][1]=static_cast<uint32_t>(r.first_count_offset);b.data.regions[i][2]=r.first_max;b.data.regions[i][3]=24;++b.data.permit[3];}
            r.first_indirect={};r.first_count={};
        }
        if(r.pass_drawn)return;
        const auto restore=impl->compatible(bound);
        if(!r.pass.supported||r.pass.extent.width!=b.first_pass.extent.width||r.pass.extent.height!=b.first_pass.extent.height||r.pass.depth!=b.first_pass.depth||!restore){b.invalid=true;return;}
        if(r.pass.kind==NativePipelineKind::gbuffer&&!b.gbuf_camera)b.gbuf_camera=impl->metadata.buffer_binding(bound,1,16);
        b.graphics_restore.push_back(restore);
        if(!impl->draw_pipelines->record(command,r.pass.kind==NativePipelineKind::gbuffer,b.calls.data(),b.calls.size(),bound.pipeline.pipeline,restore->handle,bound.sets.size(),bound.sets.data())){b.invalid=true;return;}
        const auto instance=impl->metadata.buffer_binding(bound,2,4);
        if(!same_binding(b.instances,instance))b.invalid=true;
        b.sources.push_back(instance);
        r.pass_drawn=true;if(r.pass.kind==NativePipelineKind::visibility)++b.visibility_draws;else if(r.pass.kind==NativePipelineKind::gbuffer)++b.gbuffer_draws;
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::before_end_rendering(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);if(found!=impl->commands.end())impl->mixed_end(found->second,command);}catch(...){scope_unsupported(command,"mixed OIT insertion failed");}
}
void NativeBackend::end_rendering(VkCommandBuffer command)noexcept {try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found!=impl->commands.end()){auto& r=found->second;if(r.bundle&&r.bundle->material_plan.mixed&&r.bundle->oit_window.active){r.bundle->oit_window.end(false);r.bundle->invalid=true;}if(r.pass.supported&&r.pass.kind==NativePipelineKind::gbuffer)r.view_gbuffer=true;r.pass_active=false;r.observed_oit_active=false;}}catch(...){} }
bool NativeBackend::pose_capture_boundary(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);
        return found!=impl->commands.end()&&found->second.eligible&&found->second.pass_active&&
            found->second.pass.supported&&found->second.pass.kind==NativePipelineKind::gbuffer&&!impl->context.uncertain;
    }catch(...){return false;}
}
void NativeBackend::dispatch(VkCommandBuffer command,bool capture_owner,std::uint32_t x,std::uint32_t y,std::uint32_t z)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;r.compute_producer=true;
        impl->mixed_dispatch(r,command,capture_owner,x,y,z);
        impl->observe_volume(r,command,capture_owner,x,y,z);
        if(!r.bundle || !capture_owner)return;
        const auto bound=impl->metadata.bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);if(!bound.valid||bound.pipeline.kind!=NativePipelineKind::lighting)return;
        const auto owner=impl->metadata.buffer_binding(bound,2,24);if(r.bundle->owner&&!same_binding(r.bundle->owner,owner))r.bundle->invalid=true;else r.bundle->owner=owner;
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::barriers(VkCommandBuffer command,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags flags,
    std::uint32_t memory_count,const VkMemoryBarrier* memory,std::uint32_t buffer_count,const VkBufferMemoryBarrier* buffers,std::uint32_t image_count,const VkImageMemoryBarrier* images)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);if(found!=impl->commands.end())impl->transparency_trace.legacy(found->second.transparency,source,destination,flags,memory_count,memory,buffer_count,buffers,image_count,images);}catch(...){}
}
void NativeBackend::barriers2(VkCommandBuffer command,const VkDependencyInfo* info)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);if(found!=impl->commands.end())impl->transparency_trace.sync2(found->second.transparency,info);}catch(...){}
}
void NativeBackend::unsupported_draw(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);
        if(impl->replacement)return; // Explicit indexed hooks apply coverage at the actual draw below.
        if(found!=impl->commands.end() && found->second.pass_active && found->second.pass.supported){
            const auto bound=impl->metadata.bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
            if(bound.valid && bound.pipeline.kind==found->second.pass.kind){found->second.tainted=true;impl->notice("noncounted model draw inside a matched native pass");}}}
    catch(...) {scope_unsupported(command);}
}
void NativeBackend::uncovered_draw(VkCommandBuffer command,const char* command_name)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);
        if(found!=impl->commands.end()){impl->mixed_late(found->second,command);impl->observe_transparent_draw(found->second,command);}
        if(impl->replacement&&found!=impl->commands.end()&&found->second.pass_active&&found->second.pass.supported) {
            const auto observed=impl->metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
            auto& recording=found->second;
            impl->observe_ownership_draw(recording,observed);
            if(command_name&&recording.eligible&&!recording.tainted&&!recording.native_restore&&recording.bundle&&
               !recording.bundle->invalid&&!impl->context.uncertain&&observed.valid){
                const bool mesh=observed.pipeline.mesh_bytes!=0;
                const bool ordinary=!std::strcmp(command_name,"vkCmdDraw")||!std::strcmp(command_name,"vkCmdDrawIndexed")||!std::strcmp(command_name,"vkCmdDrawIndirect");
                const bool mesh_call=!std::strcmp(command_name,"vkCmdDrawMeshTasksEXT")||!std::strcmp(command_name,"vkCmdDrawMeshTasksIndirectEXT")||!std::strcmp(command_name,"vkCmdDrawMeshTasksIndirectCountEXT");
                const auto coverage=classify_native_body_coverage(body_identity(observed.pipeline));
                if(((ordinary&&!mesh)||(mesh_call&&mesh))&&coverage.kind!=NativeBodyCoverageKind::unknown&&coverage.kind!=NativeBodyCoverageKind::chunklet){
                    impl->cover_native_draw(recording,command);return;
                }
            }
            // Only these exact native representations remain untouched. They
            // cannot establish the first counted Model witness or clear any
            // previous decline, and never receive private pipeline/bind state.
            if(command_name&&!std::strcmp(command_name,"vkCmdDrawIndirect")&&recording.eligible&&
               recording.pass.kind==NativePipelineKind::visibility&&!recording.tainted&&!recording.native_restore&&
               recording.bundle&&!recording.bundle->invalid&&!impl->context.uncertain&&observed.valid){
                const auto coverage=classify_native_body_coverage(body_identity(observed.pipeline));
                if(coverage.kind==NativeBodyCoverageKind::chunklet){
                    auto& bundle=*recording.bundle;
                    const auto source=impl->metadata.observed_buffer_binding(observed,2,coverage.transform_binding);
                    if(bundle.sources.size()<2048&&source&&(source.usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)&&
                       source.info.range!=VK_WHOLE_SIZE&&source.info.range>=176&&impl->source_current(source)){
                        bundle.sources.push_back(source);
                        ++impl->coverage_independent;impl->notice("replacement-preview exact chunklet draws unchanged; coverage is per-primary and incomplete","replacement-chunklet-passthrough");return;
                    }
                }
            }
            ++impl->coverage_uncovered;recording.tainted=true;impl->unknown_ownership(recording,"uninstrumented-draw");if(recording.bundle)recording.bundle->invalid=true;
            const auto vertex=digest_text(observed.pipeline.vertex_digest),fragment=digest_text(observed.pipeline.fragment_digest);
            const std::string name=command_name?command_name:"unspecified";
            const auto key="uninstrumented-"+name+"-"+vertex+"-"+fragment;
            if(impl->wants_notice(key))impl->notice("replacement declined uninstrumented command="+name+
                " observed-valid="+std::to_string(observed.valid)+" stages="+std::to_string(observed.pipeline.graphics_stage_count)+
                " tess-control="+std::to_string(observed.pipeline.tess_control_bytes)+":"+digest_text(observed.pipeline.tess_control_digest)+
                " tess-evaluation="+std::to_string(observed.pipeline.tess_evaluation_bytes)+":"+digest_text(observed.pipeline.tess_evaluation_digest)+
                " vertex="+std::to_string(observed.pipeline.vertex_bytes)+":"+vertex+
                " fragment="+std::to_string(observed.pipeline.fragment_bytes)+":"+fragment+" | "+
                impl->metadata.binding_diagnostic(command,VK_PIPELINE_BIND_POINT_GRAPHICS),key);
        }
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::before_native_draw(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->replacement)return;
        const auto found=impl->commands.find(command);if(found==impl->commands.end())return;auto& r=found->second;
        impl->mixed_late(r,command);impl->observe_transparent_draw(r,command);
        impl->observe_ownership_draw(r,impl->metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS));
        impl->cover_native_draw(r,command);
        impl->diagnostic_shadow_draw(r,command);
        impl->caster_shadow_draw(r,command);
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::after_native_draw(VkCommandBuffer command)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);const auto found=impl->commands.find(command);
        if(found!=impl->commands.end()&&found->second.native_restore) {
            impl->function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_GRAPHICS,found->second.native_restore);
            found->second.native_restore=VK_NULL_HANDLE;
        }
    }catch(...){scope_unsupported(command);}
}
void NativeBackend::scope_unsupported(VkCommandBuffer command,const char* reason)noexcept {try{std::lock_guard<std::mutex> lock(impl->mutex);auto found=impl->commands.find(command);if(found!=impl->commands.end()){
    found->second.tainted=true;impl->unknown_ownership(found->second,"unsupported-scope");if(reason)impl->notice("tainted-command "+std::string(reason));}}catch(...){} }
void NativeBackend::command_end(VkCommandBuffer command,VkResult result)noexcept {try{std::lock_guard<std::mutex> lock(impl->mutex);impl->each_view(command,[&](VkCommandBuffer,Recording& r){r.ownership_closed=result==VK_SUCCESS&&!r.pass_active;if(r.bundle)r.bundle->ended=result==VK_SUCCESS;impl->finish_transparency(r,result);});}catch(...){} }
VkResult NativeBackend::before_submit(VkQueue queue,uint32_t count,const VkSubmitInfo* submits)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        for(uint32_t i=0;i<count;++i)for(uint32_t j=0;j<submits[i].commandBufferCount;++j){const auto gate=impl->retry_gate(queue,submits[i].pCommandBuffers[j]);if(gate!=VK_SUCCESS)return gate;}
        for(uint32_t i=0;i<count;++i){const auto& s=submits[i];const bool shape=native_legacy_submit_supported(s)&&queue==impl->queue&&!impl->disabled&&!impl->context.uncertain;
        for(uint32_t j=0;j<s.commandBufferCount;++j)impl->submit_command(s.pCommandBuffers[j],shape,queue);
    }impl->finish_history_submission(queue);return VK_SUCCESS;}catch(...){impl->discard_history();return VK_ERROR_OUT_OF_HOST_MEMORY;}
}
VkResult NativeBackend::before_submit2(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);
        for(uint32_t i=0;i<count;++i)for(uint32_t j=0;j<submits[i].commandBufferInfoCount;++j){const auto gate=impl->retry_gate(queue,submits[i].pCommandBufferInfos[j].commandBuffer);if(gate!=VK_SUCCESS)return gate;}
        for(uint32_t i=0;i<count;++i)for(uint32_t j=0;j<submits[i].commandBufferInfoCount;++j)impl->submit_command(submits[i].pCommandBufferInfos[j].commandBuffer,false,queue);
        impl->finish_history_submission(queue);return VK_SUCCESS;
    }catch(...){return VK_ERROR_OUT_OF_HOST_MEMORY;}
}
void NativeBackend::submission_result(VkQueue queue,VkResult result)noexcept {
    try{std::lock_guard<std::mutex> lock(impl->mutex);bool failed=false;
        struct Result {unsigned observed=0,candidates=0;std::shared_ptr<SubmittedHistory> emission;};
        std::map<Impl::HistoryKey,Result> views;
        impl->each_recording([&](VkCommandBuffer,Recording& item){if(item.bundle){auto& b=*item.bundle;if(b.awaiting_submit_result&&b.submitted_queue==queue){
            auto& view=views[Impl::history_key(b.instances.info.buffer,b.first_pass.extent)];
            ++view.observed;b.awaiting_submit_result=false;
            if(result!=VK_SUCCESS){b.failed_submit=b.invalid=true;failed=true;}
            else if(b.data.permit[0]&&b.emission&&impl->publication_for(b.instances.info.buffer)&&impl->publication_for(b.instances.info.buffer)->serial==b.pose->serial){
                ++view.candidates;view.emission=b.emission;view.emission->owner=std::move(b.cpu_owner);
                view.emission->menu_preview=b.menu_preview;view.emission->menu_owner=std::move(b.menu_owner);
            }
        }}});
        for(auto& pair:views){auto& view=pair.second;
            impl->update_history(pair.first,result==VK_SUCCESS&&view.observed==1&&view.candidates==1?std::move(view.emission):std::shared_ptr<SubmittedHistory>{});}

        if(failed){impl->revoke_preparation();impl->last_submit_error=result;impl->disabled=true;impl->recover_submission(queue);impl->report("native submit failed; private preparation stopped; retries quarantined until proven queue completion");}
    }catch(...){impl->discard_history();}
}
}
