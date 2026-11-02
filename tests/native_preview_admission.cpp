#include "../src/native_preview_subject.hpp"
#include "../src/native_menu_owner.hpp"
#include "../src/native_publication.hpp"
#include "../src/native_state.hpp"
#include "../src/native_body_coverage.hpp"
#include <map>
#include <cstring>
#include <type_traits>
#include <iostream>
#include <stdexcept>
using namespace evrm;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Clip {NativeClipKind kind=NativeClipKind::visibility;bool mesh=false;VkPipelineLayout original_layout{},layout{};VkPipeline handle{};};
struct Restore {VkPipelineLayout handle{};};
struct Pass {bool supported=true;NativePipelineKind kind=NativePipelineKind::visibility;VkExtent2D extent{640,480};VkImageView depth{};};
struct Bundle {NativeOwnerCandidate cpu_owner;NativeMenuOwnerCandidate menu_owner;NativeBufferBinding instances,culled,vis_camera,gbuf_camera,indirect,counts;
    std::shared_ptr<const NativeAvatarPublication> pose;NativePreviewStamp preview_subject;bool menu_preview=true,invalid=false,body_bits=true,first_regions_open=false;
    VkDescriptorSet clip_set=reinterpret_cast<VkDescriptorSet>(30);Pass first_pass;
    std::vector<NativeBufferBinding> sources,excluded_transforms;std::vector<std::shared_ptr<Clip>> clip_leases;
    unsigned clipped_draws=0,gbuffer_clips=0,visibility_clips=0,visibility_draws=0,gbuffer_draws=0;
    std::vector<int> calls;std::vector<std::shared_ptr<Restore>> graphics_restore;
    struct {unsigned permit[4]{},regions[32][4]{};}data;
};
struct Recording {std::shared_ptr<Bundle> bundle;bool pass_active=true,tainted=false,ownership_unknown=false,pass_drawn=false;Pass pass;
    VkPipeline native_restore{};VkBuffer first_indirect{},first_count{};VkDeviceSize first_offset=0,first_count_offset=0;unsigned first_stride=24,first_max=1;};
unsigned pipeline_binds=0,descriptor_binds=0,vrm_draws=0;VkPipeline last_pipeline{};
void VKAPI_CALL bind_pipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline p){++pipeline_binds;last_pipeline=p;}
void VKAPI_CALL bind_sets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned,const VkDescriptorSet*,unsigned,const unsigned*){++descriptor_binds;}
namespace evrm {
#include "native_preview_binding.inc"
// External shader classifier port: this fixture tests the actual admission and
// command-writing body, while existing coverage tests pin original shaders.
NativeBodyCoverage classify_native_body_coverage(const NativeBodyShaderIdentity&)noexcept{return {NativeBodyCoverageKind::clip,NativeClipKind::visibility,4,5,0};}
}
NativeBodyShaderIdentity body_identity(const NativePipelineState&){return {};}
bool same_binding(const NativeBufferBinding& a,const NativeBufferBinding& b){return a.info.buffer==b.info.buffer&&a.info.offset==b.info.offset&&a.info.range==b.info.range&&a.generation==b.generation;}
struct Backend {
    struct {VkDevice device=reinterpret_cast<VkDevice>(2);}context;
    mutable unsigned verifies=0;bool verify=true;mutable bool change_actor=false;
    bool revalidate_owner(NativeOwnerCandidate& owner,std::string&)const noexcept{++verifies;if(change_actor)++owner.entity_generation;return verify;}
    std::mutex mutex;std::map<VkCommandBuffer,Recording> commands;
    unsigned coverage_sources=0,coverage_independent=0,coverage_models=0;
    struct Metadata {
        NativeBoundState value;NativeBufferBinding instance,culled,camera;
        NativeBoundState observed_bound(VkCommandBuffer,VkPipelineBindPoint)const{return value;}
        NativeBoundState bound(VkCommandBuffer,VkPipelineBindPoint)const{return value;}
        NativeBufferBinding observed_buffer_binding(const NativeBoundState&,unsigned,unsigned binding)const{return binding==4?instance:culled;}
        NativeBufferBinding buffer_binding(const NativeBoundState&,unsigned set,unsigned binding)const{return set==1?camera:binding==4?instance:culled;}
    }metadata;
    struct {bool valid(VkBuffer,VkDeviceSize,VkDeviceSize){return true;}bool disjoint(VkBuffer,VkDeviceSize,VkDeviceSize,VkBuffer,VkDeviceSize,VkDeviceSize){return true;}}memory;
    struct Clips {std::shared_ptr<Clip> value=std::make_shared<Clip>();auto pipeline(VkPipeline){return value;}};
    std::shared_ptr<Clips> clips=std::make_shared<Clips>();
    struct Draws {template<class... Args>bool record(Args&&...){++vrm_draws;return true;}};
    std::shared_ptr<Draws> draw_pipelines=std::make_shared<Draws>();
    std::shared_ptr<Restore> compatible(const NativeBoundState& state){auto r=std::make_shared<Restore>();r->handle=state.pipeline.layout;return r;}
    bool source_current(const NativeBufferBinding&)const{return true;}
    void notice(const char*){}void clip_diagnostic(const NativeBoundState&,const char*,const char*){}
    template<class F>F function(const char* name){
        if constexpr(std::is_same_v<F,PFN_vkCmdBindPipeline>){require(!std::strcmp(name,"vkCmdBindPipeline"),"wrong pipeline port");return &bind_pipeline;}
        else{require(!std::strcmp(name,"vkCmdBindDescriptorSets"),"wrong descriptor port");return &bind_sets;}
    }
#include "native_preview_admission.inc"
#include "native_preview_cover.inc"
};
struct FixtureNativeBackend {Backend* impl;void render_current(VkCommandBuffer)noexcept;void after_native_draw(VkCommandBuffer)noexcept;void scope_unsupported(VkCommandBuffer){throw std::runtime_error("actual VRM command fixture missed a dependency");}};
#include "native_preview_render.inc"
NativePreviewStamp publish(NativePreviewSubject kind){auto& core=native_preview_subjects();const auto c=core.source();const auto ticket=core.begin_request(c.epoch);core.finish_request(ticket,kind);return core.copied(core.copy_proposal(c.epoch));}
int main(){try{
    Backend backend;Bundle bundle;std::string reason;
    auto& actor=bundle.cpu_owner;actor.image.base=1;actor.client=2;actor.controller=3;actor.main=4;actor.execution=5;actor.registry=6;actor.local_player_data=7;actor.entity=8;actor.entity_id=9;actor.entity_generation=10;
    actor.guards=NativeOwnerGuards({NativeOwnerGuard{100,{1},NativeOwnerObservationPhase::owners}});
    bundle.menu_owner.kind=NativeMenuOwnerKind::character;bundle.menu_owner.image.base=1;bundle.menu_owner.client=2;bundle.menu_owner.controller=3;bundle.menu_owner.preview.main=4;bundle.menu_owner.renderer=15;
    bundle.menu_owner.preview.native_buffer=13;bundle.instances.info.buffer=reinterpret_cast<VkBuffer>(13);bundle.instances.generation=14;
    auto pose=std::make_shared<NativeAvatarPublication>();bundle.pose=pose;
    require(!backend.prepare_character_subject(bundle,true,reason),"unknown captured preview still passed actual Character admission");
    auto local=publish(NativePreviewSubject::local);pose->preview_subject=local;native_preview_subject_thread().working=local;
    require(backend.prepare_character_subject(bundle,true,reason)&&backend.character_subject_draw(bundle),"proved inventory subject failed admission");
    require(backend.verify_character_subject(bundle,reason)&&backend.verifies==1,"submission omitted fresh full actor verification");
    {
        FixtureNativeBackend runtime{&backend};const auto command=reinterpret_cast<VkCommandBuffer>(1000);
        auto draw=std::make_shared<Bundle>(bundle);draw->instances.info.range=128;draw->instances.size=128;draw->instances.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        auto& state=backend.metadata;state.instance=draw->instances;state.culled=draw->instances;state.culled.info.range=8;state.culled.info.buffer=reinterpret_cast<VkBuffer>(17);
        state.camera=draw->instances;draw->culled=state.culled;draw->vis_camera=state.camera;state.value.valid=true;state.value.pipeline.kind=NativePipelineKind::visibility;
        state.value.pipeline.pipeline=reinterpret_cast<VkPipeline>(18);state.value.pipeline.layout=reinterpret_cast<VkPipelineLayout>(19);
        backend.clips->value->original_layout=state.value.pipeline.layout;backend.clips->value->layout=reinterpret_cast<VkPipelineLayout>(20);backend.clips->value->handle=reinterpret_cast<VkPipeline>(21);
        auto& recording=backend.commands[command];recording.bundle=draw;
        runtime.render_current(command);require(vrm_draws==1&&!draw->invalid,"actual local VRM insertion failed");
        backend.cover_native_draw(recording,command);require(pipeline_binds==1&&descriptor_binds==1&&!draw->invalid,"actual local native clipping failed");
        runtime.after_native_draw(command);require(pipeline_binds==2&&last_pipeline==state.value.pipeline.pipeline&&!recording.native_restore,"actual original pipeline restoration failed");
        native_preview_subject_thread().working=publish(NativePreviewSubject::other);
        backend.cover_native_draw(recording,command);runtime.render_current(command);
        require(draw->invalid&&recording.tainted&&pipeline_binds==2&&descriptor_binds==1&&vrm_draws==1,
            "actual same-buffer survivor draw was clipped or received a VRM");
        backend.commands.clear();native_preview_subject_thread().working=local;
    }
    // A future publication has no power to relabel earlier recorded work.
    auto other=publish(NativePreviewSubject::other);require(backend.character_subject_draw(bundle),"later global request relabeled earlier local work");
    native_preview_subject_thread().working=other;
    require(!backend.character_subject_draw(bundle),"survivor draw with identical source passed local clipping gate");
    pose->preview_subject=other;require(!backend.prepare_character_subject(bundle,true,reason),"other captured pose was authorized by local renderer allocation");
    auto newer_local=publish(NativePreviewSubject::local);native_preview_subject_thread().working=newer_local;
    require(!backend.prepare_character_subject(bundle,true,reason),"new local request relabeled earlier other capture");
    pose->preview_subject=local;require(backend.prepare_character_subject(bundle,true,reason),"fresh local work could not use separately proved same-actor pose");
    backend.verify=false;require(!backend.verify_character_subject(bundle,reason),"failed actor guards granted native-body suppression");backend.verify=true;
    backend.change_actor=true;require(!backend.verify_character_subject(bundle,reason),"changed actor generation survived submission");backend.change_actor=false;
    ++actor.entity_generation;require(!backend.prepare_character_subject(bundle,true,reason),"actor generation reused an old menu capture");
    auto current=publish(NativePreviewSubject::local);pose->preview_subject=current;native_preview_subject_thread().working=current;
    require(backend.prepare_character_subject(bundle,true,reason),"new actor did not acquire a distinct preview cohort");
    ++bundle.instances.generation;require(!backend.prepare_character_subject(bundle,true,reason),"reused buffer allocation retained old preview authority");
    current=publish(NativePreviewSubject::local);pose->preview_subject=current;native_preview_subject_thread().working=current;
    require(backend.prepare_character_subject(bundle,true,reason),"current allocation did not admit its fresh local subject");
    native_preview_subjects().begin_lifetime();require(!backend.verify_character_subject(bundle,reason),"main destruction retained actual backend submission authority");
    native_preview_subjects().end_lifetime();
    require(!backend.prepare_character_subject(bundle,true,reason),"identical actor fields and live Character allocation relabeled an old-world pose");
    current=publish(NativePreviewSubject::local);pose->preview_subject=current;native_preview_subject_thread().working=current;
    require(backend.prepare_character_subject(bundle,true,reason)&&backend.verify_character_subject(bundle,reason),"new world could not admit new local work after identical-value reuse");
    bundle.menu_owner.kind=NativeMenuOwnerKind::frontend;require(backend.prepare_character_subject(bundle,false,reason)&&backend.character_subject_draw(bundle),"local preview gate changed frontend ownership");
    native_preview_subject_thread()={};native_preview_subjects().retire_owner(reinterpret_cast<std::uintptr_t>(&backend));
    std::cout<<"PASS actual Character admission/draw/submit: unknown/other stay native, old batches immutable, fresh actor/source guards and frontend separate\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
