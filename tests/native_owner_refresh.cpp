// Actual production census refresh and submit-owner branch, with external
// process reads, source metadata and clock supplied by CPU-only ports.
#include "../src/native_pose_owner.hpp"
#include "../src/native_body_selection.hpp"
#include "../src/native_instance_qualifiers.hpp"
#include "../src/native_publication.hpp"
#include "../src/native_submit_timing.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace evrm;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::uint64_t now=1000;
bool quantized_tick=false;
std::uint64_t test_tick(){return quantized_tick?(now/16)*16:now;}
template<class F>void log_event_lazy(F&&){} // No admission decision depends on logging.
struct Bundle {
    struct OwnerRefreshTrace {
        bool attempted=false,verified=false,observation_retained=false;
        double capture_ms=0,verify_ms=0,total_ms=0;
        std::uint64_t pose_age_start=0,pose_age_end=0;
    } owner_refresh;
    std::shared_ptr<const NativeAvatarPublication> pose;
    NativeOwnerCandidate cpu_owner;
    NativeMenuOwnerCandidate menu_owner;
    bool menu_preview=false;
    std::shared_ptr<const NativeOwnerCandidate> prepared_owner_hint;
    std::vector<NativeBufferBinding> sources,excluded_transforms;
    NativeBufferBinding instances,gbuf_camera,owner;
    NativeBodyMask body_mask;
    std::vector<std::array<std::uint32_t,4>> qualified_parts;
    struct {bool mixed=false;}material_plan;
    struct {bool good=true;bool complete()const{return good;}}oit_window;
};
NativePoseOwner fixture(){
    NativePoseOwner value;value.source_epoch=9;
    auto& source=value.source;source.info={reinterpret_cast<VkBuffer>(0x501),0,384};source.size=384;
    source.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;source.generation=7;
    source.set=reinterpret_cast<VkDescriptorSet>(0x601);source.set_generation=1;source.set_version=2;
    auto& owner=value.candidate;owner.image.base=0x140000000;owner.image.size=0x100000;owner.image.sha256[0]=1;
    owner.root=0x10000;owner.client=0x11000;owner.controller=0x12000;owner.entity=0x13000;owner.entity_id=4;owner.entity_generation=3;
    owner.native_buffer=0x501;owner.native_buffer_bytes=384;owner.instance_capacity=3;
    NativeOwnerModel model;model.index=0;model.generation=1;model.handle=0x01000000;model.record=0x20000;owner.models={model};
    NativeOwnerGuard guard;guard.address=0x30000;guard.bytes={1};owner.guards=NativeOwnerGuards({guard});return value;
}
struct Impl {
    NativeSubmitTiming submit_timing;
    NativePoseOwner initial=fixture();
    NativePoseOwnerState pose_owner_state;
    struct Metadata {
        NativeBufferBinding source;
        auto buffer(VkBuffer requested)const{need(requested==source.info.buffer,"queried unrelated source");return source;}
        std::uint64_t buffer_epoch()const{return 9;}
        bool live(const NativeBufferBinding& value)const{return same_native_pose_allocation(value,source);}
    }metadata{initial.source};
    struct Memory {
        bool backing=true,separate=true;
        bool valid(VkBuffer,std::uint64_t,std::uint64_t)const{return backing;}
        bool disjoint(VkBuffer,std::uint64_t,std::uint64_t,VkBuffer,std::uint64_t,std::uint64_t)const{return separate;}
    }memory;
    bool capture_ok=true,verify_ok=true,changed_owner=false,render_ok=true,mixed_ok=true;
    unsigned captures=0,verifies=0,selection_captures=0;
    std::uint64_t capture_ticks=2,verify_ticks=2,initial_verify_ticks=0,census_refreshes=0;
    static std::uint64_t owner_clock_milliseconds()noexcept{return now;}
    std::shared_ptr<const NativeAvatarPublication> latest,world;
    bool same_owner_selection(const NativeOwnerCandidate& a,const NativeOwnerCandidate& b)const{return same_native_owner_selection(a,b);}
    auto publication_for(VkBuffer source)const{need(source==initial.source.info.buffer,"refresh looked up another view");return world;}
    void revoke_preparation(){pose_owner_state.revoke_preparation();}
    bool sources_current(const std::vector<NativeBufferBinding>&)const{return render_ok;}
    bool source_current(const NativeBufferBinding&)const{return render_ok;}
    bool mixed_current(const Bundle&)const{return mixed_ok;}
    bool capture_owner(NativeOwnerCandidate& out,std::string& reason,NativeOwnerObservationStats*,std::uint64_t){
        ++captures;now+=capture_ticks;
        if(!capture_ok){reason="test capture failed";return false;}
        out=initial.candidate;if(changed_owner)++out.entity_generation;
        NativeOwnerGuard fresh;fresh.address=0x30000;fresh.bytes={2};out.guards=NativeOwnerGuards({fresh});return true;
    }
    bool revalidate_owner(NativeOwnerCandidate& out,std::string& reason,NativeOwnerObservationStats* stats,std::uint64_t limit=40){
        ++verifies;
        if(out.guards.empty()||out.guards[0].bytes[0]==1){
            now+=initial_verify_ticks;
            if(stats){stats->phase=NativeOwnerObservationPhase::submission_verify;stats->failure_location=NativeOwnerFailureLocation::compare;
                stats->guard_capture_phase=NativeOwnerObservationPhase::census;}
            out={};reason="test census changed";return false;
        }
        now+=verify_ticks;
        if(verify_ticks>=limit){out={};reason="test verification deadline";return false;}
        if(!verify_ok){out={};reason="test verification failed";return false;}return true;
    }
    bool revalidate_menu_owner(NativeMenuOwnerCandidate&,std::string&){throw std::runtime_error("world test entered menu path");}
    bool verify_character_subject(Bundle&,std::string&){throw std::runtime_error("world test entered preview subject path");}
    // Both generated includes are sliced from the current production source.
#define GetTickCount64 test_tick
#include "native_owner_refresh.inc"
    bool submit_owner(Bundle& b,std::string& reason){
        bool ready=true;struct {bool check(bool value,int){return value;}}decision;
        struct Gate {enum {OwnerFreshness};};
#include "native_owner_submit.inc"
        return ready;
    }
#undef GetTickCount64
    auto sample(){return pose_owner_state.sample(9,now,
        [&](auto& out){++selection_captures;out=initial.candidate;return true;},
        [&](auto buffer){return metadata.buffer(buffer);},
        [&](const auto& source){return metadata.live(source)&&memory.valid(source.info.buffer,0,source.size);});}
    Bundle bundle(){
        Bundle b;auto pose=std::make_shared<NativeAvatarPublication>();pose->asset=std::make_shared<vrm::Avatar>();
        pose->published_milliseconds=920;pose->serial=5;pose->source_buffer_epoch=9;
        pose->parts={{0,100,0,1}};pose->instance_qualifiers=pose->parts;pose->source_instances=0x501;pose->source_instance_range=384;
        b.pose=pose;world=latest=pose;b.cpu_owner=initial.candidate;
        auto selected=sample();need(bool(selected),"initial ownership not observed");pose_owner_state.retain_preparation(selected);
        b.prepared_owner_hint=std::shared_ptr<const NativeOwnerCandidate>(selected,&selected->candidate);
        b.instances=b.gbuf_camera=b.owner=initial.source;b.qualified_parts=pose->parts;b.body_mask.words[0]=1;b.body_mask.selected=1;return b;
    }
};
template<class I>auto refresh(I& impl,Bundle& b,std::string& reason,NativeOwnerObservationStats& stats,bool& observed,int)
    ->decltype(impl.refresh_census_owner(b,now,reason,stats,observed)){return impl.refresh_census_owner(b,now,reason,stats,observed);}
template<class I>bool refresh(I& impl,Bundle& b,std::string& reason,NativeOwnerObservationStats& stats,bool& observed,long){
    observed=false;return impl.refresh_census_owner(b,now,reason,stats);
}
}
int main(int argc,char** argv){try{
    need(argc==2,"scenario required");const std::string mode=argv[1];Impl impl;auto b=impl.bundle();
    if(mode=="latest-null")impl.latest.reset();
    else if(mode=="latest-other-view"||mode=="source-publication-asset"){
        auto other=std::make_shared<NativeAvatarPublication>(*b.pose);other->asset=std::make_shared<vrm::Avatar>();
        if(mode=="latest-other-view")impl.latest=other;else impl.world=other;
    }else if(mode=="source-publication-missing")impl.world.reset();
    else if(mode=="expired-pose"){
        auto older=std::make_shared<NativeAvatarPublication>(*b.pose);older->published_milliseconds=760;b.pose=older;impl.world=impl.latest=older;
        impl.capture_ticks=20;impl.verify_ticks=2;
    }else if(mode=="render-source")impl.render_ok=false;
    else if(mode=="mixed-source"){b.material_plan.mixed=true;impl.mixed_ok=false;}
    else if(mode=="excluded-source"){b.excluded_transforms={impl.initial.source};impl.memory.separate=false;}
    else if(mode=="mask")b.body_mask.words[0]=2;
    else if(mode=="qualifier")b.qualified_parts[0][1]=101;
    else if(mode=="owner-changed")impl.changed_owner=true;
    else if(mode=="verify-failed")impl.verify_ok=false;
    else if(mode=="capture-failed")impl.capture_ok=false;
    else if(mode=="allocation")++impl.metadata.source.generation;
    else if(mode=="backing")impl.memory.backing=false;
    else if(mode=="deadline")impl.capture_ticks=40;
    else if(mode=="deadline-after-verify")impl.verify_ticks=40;
    else if(mode=="clock-regressed")impl.capture_ticks=std::uint64_t(-5);
    else if(mode=="clock-quantization"||mode=="clock-true-deadline"){
        quantized_tick=true;now=1007;impl.initial_verify_ticks=1;
        impl.capture_ticks=mode=="clock-quantization"?19:25;impl.verify_ticks=mode=="clock-quantization"?14:16;
    }
    else need(mode=="fresh-success","unknown scenario");
    const bool render_failure=mode=="expired-pose"||mode=="render-source"||mode=="mixed-source"||mode=="excluded-source"||mode=="mask"||mode=="qualifier";
    const auto original_stamp=b.pose->published_milliseconds;
    std::string reason;bool ready=false;
    if(mode=="latest-null"||mode=="latest-other-view"||mode=="source-publication-missing"||mode=="source-publication-asset"){
        NativeOwnerObservationStats stats;stats.phase=NativeOwnerObservationPhase::submission_verify;
        stats.failure_location=NativeOwnerFailureLocation::compare;stats.guard_capture_phase=NativeOwnerObservationPhase::census;
        bool observed=false;ready=refresh(impl,b,reason,stats,observed,0);
        need(ready==(mode=="latest-null"||mode=="latest-other-view"),"refresh used mutable global latest instead of this source's publication");
    }else{
        ready=impl.submit_owner(b,reason);
        need(ready==(mode=="fresh-success"||mode=="clock-quantization"),"refresh admitted changed/expired data or falsely rejected work within its deadline");
        auto proposed=impl.sample();
        if(render_failure){
            need(proposed&&proposed->candidate.guards[0].bytes[0]==2,"render-only decline discarded the freshly verified ownership observation");
            need(impl.selection_captures==1,"render-only decline rescanned ownership rather than retaining verified observation");
            need(!impl.pose_owner_state.preparation_hint(),"render-only decline retained a rendering permit");
        }else if(!ready){
            need(!proposed,"negative ownership or allocation evidence retained an observation");
        }
    }
    need(b.pose->published_milliseconds==original_stamp,"ownership refresh renewed the original pose time");
    if(mode=="expired-pose")need(!hold_native_publication(b.pose,9,now,6),"expired pose can still be held after ownership recovery");
    if(mode=="fresh-success")need(b.cpu_owner.guards[0].bytes[0]==2&&impl.census_refreshes==1,"successful refresh did not use newly verified guards");
    if(mode=="clock-quantization")need(now==1041&&test_tick()==1040&&impl.captures==1&&impl.verifies==2&&impl.census_refreshes==1,"coarse-clock case skipped a full ownership phase");
    if(mode=="clock-true-deadline")need(!ready&&now==1049,"actual42ms deadline was relaxed");
    std::cout<<"PASS actual owner refresh "<<mode<<" ready="<<ready<<" reason="<<reason<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
