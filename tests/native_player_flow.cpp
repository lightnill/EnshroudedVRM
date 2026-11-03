// Actual service + backend; renderer methods are extracted verbatim. The
// fixture supplies trusted pose publications instead of a live graphics bridge.
#define main backend_component_main
#include "native_player_backend.cpp"
#undef main
#include "avatar_fixture.hpp"
#include "../src/avatar_device_request.hpp"
#include "../src/avatar_switch_transaction.hpp"
#include "../src/player_assignments.hpp"
namespace evrm {
struct AvatarCpuModel {std::shared_ptr<const PreparedAvatar> prepared;AvatarCpuModel(std::shared_ptr<const PreparedAvatar> p,bool,bool):prepared(std::move(p)) {}};
struct FlowRenderer {
    struct Player {std::unique_ptr<AvatarCpuModel> model,staged_model;AvatarCandidatePtr staged;std::string staged_failure;bool staged_gpu_ready=false;std::shared_ptr<const NativeAvatarPublication> staged_publication;};
    struct Impl:Player {std::mutex resource_mutex;bool native_mode=true,gpu_skinning=true,native_replacement=true,active=true;std::map<std::string,Player> players;std::shared_ptr<const NativeAvatarPublication> publication;};
    std::unique_ptr<Impl> impl=std::make_unique<Impl>();
    std::shared_ptr<const PreparedAvatar> prepared_model()const noexcept{return impl->model?impl->model->prepared:nullptr;}
    bool stage_candidate(const AvatarCandidatePtr&,bool,std::string&)noexcept;
    bool commit_candidate(const AvatarCandidate&,std::uint64_t)noexcept;
    std::shared_ptr<const PreparedAvatar> prepared_player_model(const std::string&)const noexcept;
    bool stage_player_candidate(const AvatarCandidatePtr&,bool,std::string&)noexcept;
    bool commit_player_candidate(const AvatarCandidate&,std::uint64_t)noexcept;
    void retain_player_models(const std::vector<AvatarSubjectSelection>&)noexcept;
    void present_prefix(std::shared_ptr<const std::vector<AvatarSubjectSelection>> selected_subjects){
        const auto target_for=[&](const std::string& id)->const AvatarSubjectSelection*{for(const auto& s:*selected_subjects)if(s.subject==id)return &s;return nullptr;};
#include "flow_before_present.inc"
    }
};
#include "flow_renderer_methods.inc"
}
template<class T,class=void>struct has_ready:std::false_type{};
template<class T>struct has_ready<T,std::void_t<decltype(std::declval<T&>().player_model_ready(std::declval<const evrm::AvatarCandidate&>()))>>:std::true_type{};
template<class Backend>evrm::AvatarCandidatePtr device_request(evrm::AvatarService& service,evrm::FlowRenderer& renderer,Backend& backend,const std::string& id){
    if constexpr(has_ready<Backend>::value){const auto chosen=service.selection(id);return evrm::avatar_device_request(service,renderer.prepared_player_model(id),id,chosen&&backend.player_model_ready(*chosen));}
    else return evrm::avatar_device_request(service,renderer.prepared_player_model(id),id);
}
template<class F>void wait_flow(F f,const char* why){const auto end=GetTickCount64()+5000;do{if(f())return;Sleep(2);}while(GetTickCount64()<end);require(false,why);}
evrm::AvatarLoadResult flow_load(const std::string& path,void*){return evrm::prepare_avatar_bytes(avatar_fixture_bytes(path.find("B")!=std::string::npos?"B":"A"),path);}
struct Flow {
    BackendPlayersFixture owned{3};std::shared_ptr<BackendOwnerSource> owner=std::make_shared<BackendOwnerSource>(owned);
    std::shared_ptr<evrm::NativePlayerRoster> roster=std::make_shared<evrm::NativePlayerRoster>();
    std::unique_ptr<evrm::NativeBackend> backend;evrm::FlowRenderer renderer;std::unique_ptr<evrm::AvatarService> service;
    std::string remote,other;unsigned serial=1;
    explicit Flow(const std::filesystem::path& folder){
        owned.memory.batched=true;evrm::NativeOwnerCache cache;evrm::NativeOwnerLimits limits;std::string why;require(cache.players(owned.memory,owned.identity,*roster,why,limits),"flow roster");
        evrm::AvatarServiceOptions options;options.directory=folder;options.loader=flow_load;options.initial=flow_load("C:/fixture/A.vrm",nullptr).model;require(bool(options.initial),"flow initial model");
        service=std::make_unique<evrm::AvatarService>(options);wait_flow([&]{return service->snapshot()->preferences_loaded;},"flow preference load");service->observe_players(roster);
        for(const auto& s:*service->subjects())if(!s.subject.empty()){if(s.identity.entity_id==2)remote=s.subject;else other=s.subject;}
        require(!remote.empty()&&!other.empty(),"flow roster exports");renderer.impl->model=std::make_unique<evrm::AvatarCpuModel>(options.initial,true,true);recreate_backend();
    }
    void recreate_backend(){backend=backend_fixture();auto& b=*backend->impl;native_sources(b);backend->test_owner(owner);b.replacement=true;
        auto limits=b.limits;limits.maxComputeWorkGroupInvocations=64;limits.maxComputeWorkGroupSize[0]=64;limits.maxPerStageDescriptorStorageBuffers=limits.maxDescriptorSetStorageBuffers=8;limits.maxPerStageDescriptorUniformBuffers=limits.maxDescriptorSetUniformBuffers=1;
        b.skinning_gpu=std::make_unique<evrm::NativeSkinningGpu>(b.context.device,b.context.get,b.context.memory,limits,fixture_shaders,&b.context.uncertain);
        b.limits.maxImageDimension2D=16384;b.asset_stager=std::make_unique<evrm::NativeAvatarAssetStager>(b.context.device,b.context.get,nullptr,b.context.memory,b.limits.maxImageDimension2D,b.skinning_gpu,&b.context.uncertain);
    }
    void request(const std::string& id,const std::string& path="C:/fixture/B.vrm"){
        require(service->set_replacement_enabled(true,id)&&service->request_model(path,id),"flow request");wait_flow([&]{return bool(service->candidate(id));},"flow candidate");
    }
    void present(const std::set<std::string>& visible){
        auto targets=service->subjects();fixture_runtime_subjects=targets;backend->set_replacement_enabled(service->any_replacement_enabled());backend->retain_player_models(*targets);renderer.retain_player_models(*targets);
        std::vector<evrm::AvatarCandidatePtr> requests;std::string error;const auto queue=fake_player_gpu::handle<VkQueue>(4);
        for(const auto& target:*targets)if(auto next=device_request(*service,renderer,*backend,target.subject)){
            const auto state=backend->prepare_player_model(queue,0,next);if(state==evrm::NativeAvatarUploadState::Failed)throw std::runtime_error("flow GPU preparation failed: "+backend->impl->asset_stager->error());
            require(renderer.stage_player_candidate(next,state==evrm::NativeAvatarUploadState::Ready,error),"flow renderer staging failed");requests.push_back(next);
        }
        const auto epoch=backend->state().buffer_epoch();const auto owners=backend->player_pose_owners(epoch,*targets);renderer.present_prefix(targets);
        // Simulates captured visible parts only, after actual owned-memory
        // identity capture. Commit's production pose/epoch checks still run.
        renderer.impl->publication.reset();
        for(const auto& next:requests){const auto owner=std::find_if(owners.begin(),owners.end(),[&](const auto& p){return p.subject==next->subject;});
            auto& staged=next->subject.empty()?static_cast<evrm::FlowRenderer::Player&>(*renderer.impl):renderer.impl->players.at(next->subject);
            if(visible.count(next->subject)&&owner!=owners.end()&&staged.staged_gpu_ready){
                auto pose=std::make_shared<evrm::NativeAvatarPublication>();pose->asset=next->model->asset;pose->geometry=next->model->skin->layout;auto skin=std::make_shared<evrm::NativeSkinPose>();skin->asset=next->model->skin;pose->skinning=skin;
                pose->subject=next->subject;pose->assignment_generation=next->generation;pose->source_instances=0x12340000;pose->source_buffer_epoch=epoch;pose->serial=serial;pose->presentation_serial=serial;pose->published_milliseconds=GetTickCount64();pose->pose_owner=owner->owner;
                for(const auto& target:*targets)if(target.subject==next->subject)pose->player_identity=target.identity;
                staged.staged_publication=pose;
            }
            struct Activation {Flow* flow;const evrm::AvatarCandidate* candidate;} activation{this,next.get()};
            evrm::activate_avatar_device(*service,next,[](void* raw,const evrm::AvatarCandidate& candidate)noexcept{auto& a=*static_cast<Activation*>(raw);return a.flow->backend->activate_player_model(candidate,[](void* raw,std::uint64_t current)noexcept{auto& a=*static_cast<Activation*>(raw);return a.flow->renderer.commit_player_candidate(*a.candidate,current);},raw);},&activation);
        }
        ++serial;
    }
    void settle(const std::string& id){for(unsigned n=0;n<4;++n)present({id});require(service->active(id)&&!service->candidate(id)&&backend->impl->player_models.at(id).assets,"flow target failed to activate");}
};
int main(int argc,char** argv){SetErrorMode(0x8003);std::set_terminate([]{std::fputs("FAIL terminate\n",stderr);std::_Exit(98);});std::signal(SIGABRT,[](int){std::_Exit(99);});try{
    require(argc==4,"flow shader directory, state directory and case required");fixture_shaders=argv[1];Flow f(argv[2]);const std::string mode=argv[3];
    if(mode=="candidate"){
        require(f.service->set_replacement_enabled(false),"You None");f.request(f.remote);auto exported=f.service->subjects();
        for(const auto& s:*exported)if(s.subject==f.remote)require(!s.model&&s.candidate,"flow must start at actual candidate-only service export");
        const auto owners=f.backend->player_pose_owners(f.backend->state().buffer_epoch(),*exported);require(owners.size()==1&&owners[0].subject==f.remote,"candidate-only remote omitted from bounded capture while You=None");f.settle(f.remote);
        require(f.service->set_replacement_enabled(false,f.remote),"remote None");f.present({});require(!f.backend->impl->player_models.count(f.remote),"remote None kept GPU model");
        require(f.service->set_replacement_enabled(true,f.remote),"remote on");wait_flow([&]{return bool(f.service->candidate(f.remote));},"remote reload");f.settle(f.remote);
        wait_flow([&]{const auto data=evrm::read_player_assignments(std::filesystem::path(argv[2])/L"player-model-assignments.json");for(const auto& row:data.preferences.entries)if(row.token==f.remote)return row.replacement_enabled;return false;},"remote save");
        auto local=std::make_shared<evrm::NativePlayerRoster>(*f.roster);local->players.resize(1);f.service->observe_players(local);f.present({});f.service->observe_players(f.roster);wait_flow([&]{return bool(f.service->candidate(f.remote));},"saved-choice rejoin candidate");require(!f.service->active(f.remote),"rejoin must load candidate-only");f.settle(f.remote);
    }else if(mode=="adoption"){
        require(bool(device_request(*f.service,f.renderer,*f.backend,"")),"matching CPU model suppressed missing GPU adoption");f.settle("");const auto selected=f.service->selection();
        require(!device_request(*f.service,f.renderer,*f.backend,""),"ready same-generation device requested work");require(f.service->set_replacement_enabled(false),"local None");f.present({});require(!f.backend->impl->player_models.count(""),"local GPU retirement missing");
        require(f.service->set_replacement_enabled(true),"local on");require(f.service->selection()==selected&&!f.service->candidate(),"toggle unexpectedly reselected CPU model");f.settle("");
        f.recreate_backend();fake_player_gpu::pending=true;f.present({""});require(device_request(*f.service,f.renderer,*f.backend,"")==selected,"pending device adoption lost after first presentation");fake_player_gpu::pending=false;f.settle("");
        f.request(f.remote);f.settle(f.remote);f.recreate_backend();fake_player_gpu::pending=true;f.present({f.remote});f.present({f.remote});require(bool(device_request(*f.service,f.renderer,*f.backend,f.remote)),"remote CPU installation suppressed pending GPU adoption");fake_player_gpu::pending=false;f.settle(f.remote);
    }else if(mode=="fairness"){
        require(f.service->set_replacement_enabled(false),"You None");f.request(f.remote);f.present({});f.present({});require(f.service->candidate(f.remote)&&!f.service->active(f.remote),"off-screen A unexpectedly committed");
        f.request(f.other,"C:/fixture/A.vrm");const auto before=fake_player_gpu::submits;f.present({f.other});require(fake_player_gpu::submits>before,"off-screen Ready A monopolized upload slot and blocked B");f.settle(f.other);require(f.service->candidate(f.remote)&&!f.service->active(f.remote),"B activation changed pending A");
        require(f.service->set_replacement_enabled(false,f.remote),"A cancellation");f.present({f.other});require(!f.backend->impl->player_models.count(f.remote)&&f.backend->impl->player_models.at(f.other).assets,"A cancellation removed B or retained A");
        f.request(f.remote,"C:/fixture/A.vrm");const auto uploads=fake_player_gpu::submits;f.settle(f.remote);require(fake_player_gpu::submits==uploads,"staged/active same-model sharing uploaded twice");
        auto local=std::make_shared<evrm::NativePlayerRoster>(*f.roster);local->players.resize(1);f.service->observe_players(local);f.present({});require(f.backend->impl->player_models.empty(),"disconnect retained remote GPU state");
    }else require(false,"unknown flow case");
    std::cout<<"PASS actual service/device/backend/renderer flow "<<mode<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
