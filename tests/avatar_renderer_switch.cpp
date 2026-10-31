#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/avatar_service.hpp"
#include "../src/avatar_switch_transaction.hpp"
#include <mutex>
#include <iostream>
#include <stdexcept>
namespace evrm {
// Test seam: device-free model construction; the stage/commit methods below
// are extracted verbatim from the production renderer, not reimplemented.
struct AvatarCpuModel {std::shared_ptr<const PreparedAvatar> prepared;AvatarCpuModel(std::shared_ptr<const PreparedAvatar> p,bool,bool):prepared(std::move(p)){if(prepared->name=="fail")throw std::runtime_error("fixture failure");}};
struct AvatarRenderer {struct Impl {
    std::mutex resource_mutex;bool native_mode=true,gpu_skinning=true,native_replacement=true,staged_gpu_ready=false,active=true;
    std::unique_ptr<AvatarCpuModel> model,staged_model;AvatarCandidatePtr staged;
    std::string staged_failure;
    std::shared_ptr<const NativeAvatarPublication> publication,staged_publication;
};std::unique_ptr<Impl> impl=std::make_unique<Impl>();
bool stage_candidate(const AvatarCandidatePtr&,bool,std::string&)noexcept;bool commit_candidate(const AvatarCandidate&,std::uint64_t)noexcept;};
#include "avatar_renderer_switch_methods.inc"
std::string pose_failure(bool candidate_source_valid,bool current_valid){(void)candidate_source_valid;(void)current_valid;auto state=std::make_unique<AvatarRenderer::Impl>();auto* impl=state.get();try{throw std::runtime_error("Synthetic incompatible pose");
#include "avatar_candidate_failure.inc"
    return state->staged_failure;
}
}
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    require(!evrm::pose_failure(true,false).empty()&&evrm::pose_failure(false,false).empty(),"candidate failure requires an already working avatar instead of a validated source");
    evrm::AvatarRenderer renderer;auto& state=*renderer.impl;auto old=std::make_shared<evrm::PreparedAvatar>();old->name="old";state.model=std::make_unique<evrm::AvatarCpuModel>(old,true,true);
    auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=std::make_shared<vrm::Avatar>();auto skin=std::make_shared<evrm::NativeSkinAsset>();skin->layout=std::make_shared<evrm::NativeAvatarPose>();model->skin=skin;
    auto a=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{1,model}),b=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{2,model});std::string error;
    require(renderer.stage_candidate(a,false,error)&&state.model->prepared==old&&!renderer.commit_candidate(*a,3),"pending candidate replaced current");
    state.staged_failure="Cannot pose this model";require(!renderer.stage_candidate(a,true,error)&&state.model->prepared==old&&error==state.staged_failure,"incompatible model failure was not returned to selection service");
    require(renderer.stage_candidate(b,true,error)&&!renderer.commit_candidate(*a,3),"obsolete candidate committed");
    auto pose=std::make_shared<evrm::NativeAvatarPublication>();pose->asset=model->asset;pose->geometry=skin->layout;auto dynamic=std::make_shared<evrm::NativeSkinPose>();dynamic->asset=skin;pose->skinning=dynamic;
    pose->source_instances=9;pose->source_buffer_epoch=3;pose->serial=10;pose->published_milliseconds=GetTickCount64()-251;state.staged_publication=pose;
    require(!renderer.commit_candidate(*b,3)&&state.model->prepared==old,"stale candidate replaced current");pose->published_milliseconds=GetTickCount64();
    require(!renderer.commit_candidate(*b,4)&&state.model->prepared==old,"changed epoch replaced current");
    state.staged_failure="Another view could not pose";require(!renderer.commit_candidate(*b,3)&&state.model->prepared==old,"failed candidate view still committed");state.staged_failure.clear();
    require(renderer.commit_candidate(*b,3)&&state.model->prepared==model&&state.publication==pose&&!state.staged,"complete candidate not committed atomically");
    require(renderer.stage_candidate({},false,error)&&!state.staged_model,"cancel did not release CPU staging");
    auto bad=std::make_shared<evrm::PreparedAvatar>(*model);bad->name="fail";auto c=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{3,bad});
    require(!renderer.stage_candidate(c,true,error)&&state.model->prepared==model&&!error.empty(),"CPU staging failure replaced current");
    std::cout<<"PASS actual renderer stage/commit methods: pending/stale/superseded/epoch/failure keep current, valid atomic activation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
