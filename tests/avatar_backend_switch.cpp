#include "../src/avatar_service.hpp"
#include "../src/native_preview_subject.hpp"
#include <map>
#include <mutex>
#include <iostream>
#include <stdexcept>
namespace evrm {
using VkQueue=std::uintptr_t;
enum class NativeAvatarUploadState {Idle,Ready,Failed};
struct TestAssets {};
struct TestBundle {std::shared_ptr<TestAssets> assets;std::shared_ptr<int> descriptors=std::make_shared<int>(1);};
struct TestStager {std::shared_ptr<TestAssets> next=std::make_shared<TestAssets>();bool complete=true;
    bool cancelled=false;
    NativeAvatarUploadState update(VkQueue,std::uint32_t,std::uint64_t,const std::shared_ptr<const vrm::Avatar>& model,const std::shared_ptr<const NativeSkinAsset>& ={}){cancelled=!model;return NativeAvatarUploadState::Idle;}
    auto ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&){return complete?next:nullptr;}
    auto take_ready(std::uint64_t,const std::shared_ptr<const vrm::Avatar>&){auto result=next;next.reset();return result;}};
struct NativeBackend {struct Impl {
    std::mutex mutex;struct {bool uncertain=false;}context;bool disabled=false,replacement_enabled=true;std::uint64_t hot_generation=2;VkQueue queue=0;std::uint32_t family=0;
    std::unique_ptr<TestStager> asset_stager=std::make_unique<TestStager>();std::shared_ptr<TestAssets> assets=std::make_shared<TestAssets>();
    struct {std::uint64_t buffer_epoch(){return 3;}}metadata;
    struct {unsigned clears=0,disables=0;bool authorized=true;void clear(){++clears;authorized=false;}void disable(){++disables;authorized=false;}}tool_slots;
    std::map<unsigned,std::shared_ptr<TestBundle>> commands,completed_views;std::shared_ptr<int> latest,view_publications;
    unsigned revoked=0;void revoke_preparation(){++revoked;}
    void initialize(){}void ensure_asset_stager(){}void report(const std::string&){}
};std::unique_ptr<Impl> impl=std::make_unique<Impl>();
NativeAvatarUploadState prepare_model(VkQueue,std::uint32_t,const AvatarCandidatePtr&)noexcept;
bool activate_model(const AvatarCandidate&,bool(*)(void*,std::uint64_t)noexcept,void*)noexcept;
void set_replacement_enabled(bool)noexcept;};
#include "avatar_backend_switch_methods.inc"
}
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Commit {bool accept=true;unsigned calls=0;};
bool commit(void* raw,std::uint64_t epoch)noexcept{auto& c=*static_cast<Commit*>(raw);++c.calls;return c.accept&&epoch==3;}
int main(){try{
    evrm::NativeBackend startup;startup.impl->assets.reset();startup.impl->hot_generation=0;
    require(startup.prepare_model(1,0,{})==evrm::NativeAvatarUploadState::Idle&&!startup.impl->asset_stager->cancelled,"enabled empty candidate cancelled initial upload on a new device");
    startup.impl->replacement_enabled=false;startup.prepare_model(1,0,{});
    require(startup.impl->asset_stager->cancelled,"disabled first upload was not cancelled");
    startup.impl->replacement_enabled=true;startup.impl->assets=std::make_shared<evrm::TestAssets>();startup.impl->asset_stager->cancelled=false;startup.prepare_model(1,0,{});
    require(startup.impl->asset_stager->cancelled,"enabled model change retained superseded upload");
    startup.impl->assets.reset();startup.impl->hot_generation=23;startup.impl->asset_stager->cancelled=false;startup.prepare_model(1,0,{});
    require(startup.impl->asset_stager->cancelled,"cancelled first candidate retained upload without active assets");
    evrm::NativeBackend backend;auto& state=*backend.impl;auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=std::make_shared<vrm::Avatar>();evrm::AvatarCandidate candidate{2,model};Commit c;
    const auto old=state.assets;auto first=std::make_shared<evrm::TestBundle>();first->assets=old;state.completed_views[1]=first;std::weak_ptr<int> first_resources=first->descriptors;first.reset();
    // A second view and a current command can be recorded/submitted after the
    // upload submission. Its fence says nothing about these bundle leases.
    auto after_upload=std::make_shared<evrm::TestBundle>();after_upload->assets=old;std::weak_ptr<int> later_resources=after_upload->descriptors;state.completed_views[2]=after_upload;after_upload.reset();
    state.commands[3]=std::make_shared<evrm::TestBundle>();state.commands[3]->assets=old;
    auto stale=candidate;stale.generation=1;require(!backend.activate_model(stale,commit,&c)&&!c.calls,"stale activation reached commit");
    c.accept=false;require(!backend.activate_model(candidate,commit,&c)&&state.assets==old&&!first_resources.expired(),"rejected activation released current resources");
    require(state.tool_slots.authorized&&!state.tool_slots.clears&&!state.tool_slots.disables,"stale/rejected activation revoked current tool authorization");
    c.accept=true;require(backend.activate_model(candidate,commit,&c)&&state.assets!=old,"ready activation failed");
    require(state.completed_views.size()==2&&state.commands.size()==1&&!first_resources.expired()&&!later_resources.expired(),"activation released recorded/pending multi-view bundle resources");
    require(state.revoked==1,"activation did not reset model preparation/history");
    require(state.tool_slots.disables==1&&!state.tool_slots.clears&&!state.tool_slots.authorized,"model change must revoke old tool pose without restoring/repatching the native hook");
    const auto cached_assets=state.assets;state.latest=std::make_shared<int>(1);state.view_publications=state.latest;
    evrm::native_preview_subjects().configure({{1,2,3,4,5,6,7,8,9,10},reinterpret_cast<std::uintptr_t>(&state),11,12,13,14,0});
    backend.set_replacement_enabled(false);
    require(!state.replacement_enabled&&!state.latest&&!state.view_publications&&state.revoked==2,"disable retained replacement publication or preparation");
    require(!evrm::native_preview_subjects().active(),"no replacement retained active preview task/source authority");
    require(state.assets==cached_assets&&state.completed_views.size()==2&&state.commands.size()==1&&!first_resources.expired()&&!later_resources.expired(),"disable released assets still referenced by recorded commands");
    require(state.tool_slots.disables==2&&!state.tool_slots.clears,"disable restored/repatched tool hook instead of revoking pose");
    backend.set_replacement_enabled(false);require(state.revoked==2&&state.tool_slots.disables==2,"idle disabled path repeats revocation work");
    backend.set_replacement_enabled(true);require(state.replacement_enabled&&!state.latest&&!state.view_publications&&state.assets==cached_assets,"enable reused stale pose or discarded cached assets");
    state.completed_views.erase(1);require(first_resources.expired()&&!later_resources.expired(),"normal first-view retirement lost another command's lease");
    state.completed_views.clear();state.commands.clear();require(later_resources.expired(),"normal command retirement retained its bundle forever");
    std::cout<<"PASS actual backend activation: stale/rejected keep current, multi-view resources survive upload-ready switch until command retirement\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
