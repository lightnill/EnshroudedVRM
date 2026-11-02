#include "../src/native_pose_owner.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <chrono>

namespace evrm {class NativePoseOwnerSampler;class NativePoseOwnerState;}
namespace {
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
// Old runtime could return an existing hint, but had no pre-pose cold sampler.
// This adapter only makes the pre-implementation behavior executable for RED.
template<class T,class=void>struct Sampler {
    template<class C,class B,class V>auto sample(const std::shared_ptr<const evrm::NativePoseOwner>& current,std::uint64_t,std::uint64_t,C&&,B&&,V&&){return current;}
};
template<class T>struct Sampler<T,std::void_t<decltype(T{})>>:T{};
template<class T,class=void>struct OwnerState {
    Sampler<evrm::NativePoseOwnerSampler> sampler;
    std::shared_ptr<const evrm::NativePoseOwner> hint;
    auto preparation_hint()const{return hint;}
    void retain_preparation(std::shared_ptr<const evrm::NativePoseOwner> value){hint=std::move(value);}
    void revoke_preparation(){hint.reset();}
    void invalidate_observation(std::uint64_t){hint.reset();}
    template<class C,class B,class V>auto sample(std::uint64_t epoch,std::uint64_t now,C&& capture,B&& source,V&& valid){
        auto result=sampler.sample(hint,epoch,now,capture,source,valid);if(result)hint=result;return result;
    }
};
template<class T>struct OwnerState<T,std::void_t<decltype(T{})>>:T{};
template<class T>auto matches(const T& selection,const evrm::NativeOwnerCandidate& owner,const evrm::NativeBufferBinding& source,std::uint64_t epoch,int)
    ->decltype(native_pose_owner_matches_candidate(selection,owner,source,epoch)){return native_pose_owner_matches_candidate(selection,owner,source,epoch);}
template<class T>bool matches(const T&,const evrm::NativeOwnerCandidate&,const evrm::NativeBufferBinding&,std::uint64_t,long){return true;}
evrm::NativePoseOwner fixture(){
    evrm::NativePoseOwner result;result.source_epoch=9;result.source.info={reinterpret_cast<VkBuffer>(0x501),0,384};result.source.size=384;
    result.source.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;result.source.generation=7;
    auto& owner=result.candidate;owner.image.base=0x140000000;owner.image.size=0x100000;owner.image.sha256[0]=1;
    owner.root=0x10000;owner.client=0x11000;owner.controller=0x12000;owner.entity=0x13000;owner.entity_id=4;owner.entity_generation=3;
    owner.native_buffer=0x501;owner.native_buffer_bytes=384;owner.instance_capacity=3;
    evrm::NativeOwnerModel model;model.equipment_slot=0;model.index=0;model.generation=1;model.handle=0x01000000;model.record=0x20000;owner.models={model};
    evrm::NativeOwnerGuard guard;guard.address=0x30000;guard.bytes={1};owner.guards=evrm::NativeOwnerGuards({guard});return result;
}
struct Port {
    evrm::NativePoseOwner value=fixture();unsigned captures=0;bool capture_ok=true,backing=true;
    bool capture(evrm::NativeOwnerCandidate& out){++captures;out=capture_ok?value.candidate:evrm::NativeOwnerCandidate{};return capture_ok;}
    auto source(VkBuffer buffer){need(buffer==value.source.info.buffer,"sampler queried another allocation");return value.source;}
    bool valid(const evrm::NativeBufferBinding& source){return backing&&source.info.buffer==value.source.info.buffer&&source.generation==value.source.generation&&source.size==value.source.size;}
};
template<class S>auto sample(S& sampler,Port& port,const std::shared_ptr<const evrm::NativePoseOwner>& hint,std::uint64_t epoch=9,std::uint64_t now=100){
    return sampler.sample(hint,epoch,now,[&](auto& out){return port.capture(out);},[&](auto buffer){return port.source(buffer);},[&](const auto& source){return port.valid(source);});
}
template<class S>auto state_sample(S& state,Port& port,std::uint64_t now){
    return state.sample(9,now,[&](auto& out){return port.capture(out);},[&](auto buffer){return port.source(buffer);},[&](const auto& source){return port.valid(source);});
}
}
int main(int argc,char** argv){try{
    need(argc==2,"scenario required");const std::string mode=argv[1];Port port;Sampler<evrm::NativePoseOwnerSampler> sampler;
    if(mode=="benchmark"){
        auto& value=port.value;value.source.size=value.source.info.range=value.candidate.native_buffer_bytes=32*128;value.candidate.instance_capacity=32;
        value.candidate.models.clear();for(unsigned i=0;i<19;++i){evrm::NativeOwnerModel model;model.index=i;model.equipment_slot=i;model.handle=0x01000000u|i;model.generation=1;model.record=0x20000+256*i;value.candidate.models.push_back(model);}
        OwnerState<evrm::NativePoseOwnerState> state;auto selected=state_sample(state,port,100);need(bool(selected),"benchmark cold selection failed");state.retain_preparation(selected);
        const auto started=std::chrono::steady_clock::now();constexpr unsigned count=200000;
        for(unsigned i=0;i<count;++i)need(state_sample(state,port,101+i)==selected,"steady selection changed");
        const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
        need(port.captures==1,"steady cache benchmark repeated external ownership capture");
        std::cout<<"BENCH pure-owner-cache samples="<<count<<" models=19 us-per-sample="<<us/count<<" external-captures="<<port.captures<<'\n';
    }else if(mode=="consumer-drop"){
        const auto first=sample(sampler,port,{});need(bool(first),"no initial observation");
        need(bool(sample(sampler,port,{},9,101))&&port.captures==1,"discarded publication caused a per-frame ownership scan");
        need(bool(sample(sampler,port,{},9,349))&&port.captures==1,"successful observation was not retained during a pose gap");
        need(bool(sample(sampler,port,{},9,350))&&port.captures==2,"unadmitted observation was never refreshed");
    }else if(mode=="lifecycle-pending"||mode=="lifecycle-gap"||mode=="negative-owner"){
        OwnerState<evrm::NativePoseOwnerState> state;
        const auto first=state_sample(state,port,100);need(bool(first),"no initial lifecycle observation");
        if(mode=="lifecycle-gap")state.retain_preparation(first);
        state.revoke_preparation();need(!state.preparation_hint(),"no-publication retained render preparation");
        if(mode=="negative-owner"){
            state.invalidate_observation(101);
            need(!state_sample(state,port,102)&&port.captures==1,"negative owner evidence reused observation or bypassed cooldown");
            need(bool(state_sample(state,port,351))&&port.captures==2,"negative owner evidence prevented later recovery");
        }else{
            need(bool(state_sample(state,port,101))&&port.captures==1&&!state.preparation_hint(),"publication revocation conflated cached observations with render preparation");
            need(bool(state_sample(state,port,349))&&port.captures==1,"no-pose presentations repeatedly traversed ownership");
        }
    }else if(mode=="cold"){
        const auto result=sample(sampler,port,{});need(result&&native_pose_owner_valid(*result)&&port.captures==1,"cold ambiguous scene cannot seed ownership without a successful pose");
    }else if(mode=="reuse"||mode=="generation"||mode=="descriptor"||mode=="epoch"){
        const auto first=sample(sampler,port,{});need(bool(first),"no first selection");
        if(mode=="generation")++port.value.source.generation;
        if(mode=="descriptor"){port.value.source.set_generation=88;port.value.source.set_version=99;}
        const auto second=sample(sampler,port,first,mode=="epoch"?10:9,101);need(bool(second),"unchanged/replaced source lost ownership");
        if(mode=="generation")need(second!=first&&second->source.generation==8&&port.captures==2,"reused allocation generation retained stale proof");
        else if(mode=="epoch")need(second->source_epoch==10&&first->source_epoch==9&&port.captures==1,"epoch rebind changed old publication or rescanned ownership");
        else need(second==first&&port.captures==1,"steady capture/descriptor update performed another owner traversal");
    }else if(mode=="retry"){
        port.capture_ok=false;need(!sample(sampler,port,{},9,100)&&port.captures==1,"failed capture supplied ownership");
        port.capture_ok=true;need(!sample(sampler,port,{},9,349)&&port.captures==1,"failed reads retried every frame");
        need(bool(sample(sampler,port,{},9,350))&&port.captures==2,"failed-read cooldown never retries");
    }else if(mode=="empty"||mode=="backing"||mode=="zero-epoch"){
        if(mode=="empty")port.value.candidate.models.clear();
        if(mode=="backing")port.backing=false;
        need(!sample(sampler,port,{},mode=="zero-epoch"?0:9),"partial or unbacked capture supplied ownership");
        if(mode=="zero-epoch")need(port.captures==0,"zero epoch traversed the process");
    }else{
        auto selection=fixture();auto current=selection.candidate;auto source=selection.source;std::uint64_t epoch=9;
        if(mode=="entity")++current.entity_generation;
        else if(mode=="handle"){++current.models[0].generation;current.models[0].handle=(current.models[0].generation<<24)|current.models[0].index;}
        else if(mode=="record")++current.models[0].record;
        else if(mode=="allocation")++source.generation;
        else if(mode=="submission-epoch")++epoch;
        else if(mode=="owner-root")++current.root;
        else if(mode=="owner-service")++current.local_player_data;
        else if(mode=="census"){evrm::NativeOwnerGuard guard;guard.address=0x40000;guard.bytes={2};current.guards=evrm::NativeOwnerGuards({guard});}
        else if(mode=="submission-descriptor"){source.set_version=888;source.set_generation=999;}
        else need(mode=="same","unknown identity scenario");
        const bool expected=mode=="same"||mode=="census"||mode=="submission-descriptor";
        need(matches(selection,current,source,epoch,0)==expected,"submission reused a changed owner/allocation or rejected safe census refresh");
    }
    std::cout<<"PASS "<<mode<<" sampler/identity contract\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
