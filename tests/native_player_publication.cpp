#ifdef EVRM_PLAYER_CPU_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define main shared_retarget_fixture_main
#include "retarget_cache.cpp"
#undef main
#include "../src/avatar_model.hpp"
#include "../src/avatar_service.hpp"
#include "../src/enshrouded_bridge.hpp"
#include "../src/native_publication.hpp"
#include "../src/native_surface_material.hpp"
#include "../src/native_pose_owner.hpp"
#include "../src/native_instance_qualifiers.hpp"
#include "../src/native_tool_slots.hpp"
#include "../src/mouth_publication.hpp"
#include "../src/spring_bones.hpp"
#include "../src/spring_frame.hpp"
#include <chrono>
#include <csignal>
#include <exception>
#include <cstdlib>
namespace evrm {
unsigned report_samples=0,bridge_reads=0;
std::map<unsigned,PlayerFrame> fixture_frames;
std::wstring env(const wchar_t*){return L"";}
vrm::Matrix product(const vrm::Matrix& a,const vrm::Matrix& b){return mul(a,b);}
void log_event(const std::string&){}
template<class F> void log_event_lazy(F&&){}
bool debug_report_sources_pending()noexcept{return true;}
void debug_report_observe_player(const PlayerFrame&,std::uint64_t)noexcept{++report_samples;}
std::shared_ptr<const AvatarMenuSnapshot> avatar_runtime_snapshot()noexcept{return {};}
SecondaryHandContactMode read_native_hand_contact_mode(const NativeAvatarPublication&,std::uint64_t,std::uint64_t&,std::uint64_t*)noexcept{return SecondaryHandContactMode::None;}
struct PlayerTrace {unsigned samples=0;void sample(std::uint64_t,const PlayerFrame&,const vrm::RetargetResult&,const vrm::Avatar&){++samples;}};
EnshroudedBridge::EnshroudedBridge(const std::filesystem::path&,const std::filesystem::path&,const std::string&){}
PlayerFrame EnshroudedBridge::read_player_frame(const GpuSnapshot&,const NativePoseOwner* owner)const noexcept{++bridge_reads;return fixture_frames.at(owner?owner->candidate.entity_id:1);}
#include "player_cpu_model.inc"
}
int main(){SetErrorMode(0x8003);std::set_terminate([]{std::_Exit(98);});std::signal(SIGABRT,[](int){std::_Exit(99);});SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*)->LONG{std::_Exit(97);});try{
    auto asset=std::make_shared<vrm::Avatar>(rig());asset->version=vrm::VrmVersion::V1;
    vrm::Primitive primitive;primitive.skinned=true;primitive.indices={0,1,2};
    for(const auto& point:std::array<std::array<float,3>,3>{{{0,0,0},{1,0,0},{0,1,0}}}){vrm::Vertex vertex;vertex.position=point;vertex.normal={0,0,1};vertex.weights={1,0,0,0};primitive.vertices.push_back(vertex);}
    asset->meshes[0].primitives.push_back(primitive);asset->vertex_count=3;asset->triangle_count=1;
    auto mouth=std::make_shared<vrm::MouthData>();mouth->available=1;vrm::MouthDeltaRecord delta;delta.vertex=0;delta.position[0]={.1f,0,0};mouth->records.push_back(delta);mouth->ranges.push_back({7,0,0,1});asset->mouth=mouth;
    auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=asset;
    auto rest=vrm::retarget(*asset,source(*asset),aligned());check(bool(rest),"fixture rest rejected");
    model->skin=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(*asset,rest));check(bool(*model->skin),"fixture skin rejected");
    evrm::GpuSnapshot snapshot;snapshot.source_epoch=3;snapshot.captured_milliseconds=100;
    auto& buffer=snapshot.buffers["instances"];buffer.binding.buffer=reinterpret_cast<VkBuffer>(90);buffer.binding.range=2048;buffer.bytes.resize(2048);
    const auto owner=[&](unsigned id){auto o=std::make_shared<evrm::NativePoseOwner>();o->candidate.entity_id=id;o->candidate.entity_generation=1;o->candidate.account=100+id;o->candidate.connection=64+id;
        o->source.info.buffer=buffer.binding.buffer;o->source.generation=1;o->source_epoch=3;return o;};
    auto local_owner=owner(1),remote_owner=owner(2);
    evrm::NativePlayerIdentity local{101,1,65,true},remote{102,2,66,false};
    evrm::PlayerFrame frame;frame.source_pose=source(*asset);frame.actor_world=identity;frame.parts.push_back({1,0,0,{},{},{}});evrm::fixture_frames[1]=frame;
    frame.parts[0].instance_id=2;frame.source_pose.current.at("hips")[12]=.35f;evrm::fixture_frames[2]=frame;
    evrm::AvatarCpuModel a(model,true,true),b(model,true,true);evrm::EnshroudedBridge bridge({}, {}, {});std::array<double,5> timings{};evrm::PlayerTrace trace;
    evrm::MouthSnapshot voice;voice.epoch=1;voice.timestamp_us=1000;voice.activity=evrm::VoiceActivity::Talking;voice.weights={1,0,0,0,0};
    auto pa=a.build_native_view(snapshot,8,9,bridge,voice,1,0,timings,&trace,nullptr,local_owner,nullptr,"",local,7);
    auto pb=b.build_native_view(snapshot,8,9,bridge,voice,1,0,timings,&trace,nullptr,remote_owner,nullptr,"remote",remote,8);
    check(pa->asset==pb->asset&&pa->skinning->asset==pb->skinning->asset&&pa->skinning!=pb->skinning,"immutable asset sharing lost or mutable skin shared");
    check(pa->skinning->matrices!=pb->skinning->matrices,"two actors share posed matrices");
    check(pa->mouth_weights[0]>0&&pb->mouth_weights==vrm::MouthWeights{},"remote consumed local voice");
    check(evrm::report_samples==1&&trace.samples==1,"remote contributed local diagnostics");
    check(a.view_motion.at(90).retargeter!=b.view_motion.at(90).retargeter,"retarget histories shared");
    b.view_motion.at(90).contact.owner=42;check(!a.view_motion.at(90).contact.owner,"contact histories shared");
    b.bind_motion(90,remote_owner,3,{});check(b.view_motion.count(90),"same actor discarded history");
    auto changed=std::make_shared<evrm::NativePoseOwner>(*remote_owner);++changed->candidate.entity_generation;
    b.bind_motion(90,changed,3,{});check(!b.view_motion.count(90)&&a.view_motion.count(90),"actor generation reused history or erased another player");
    b.motion(90).contact.owner=42;b.bind_motion(90,changed,4,{});check(!b.view_motion.count(90),"source epoch reused history");
    b.motion(90).contact.owner=42;auto reallocated=std::make_shared<evrm::NativePoseOwner>(*changed);++reallocated->source.generation;
    b.bind_motion(90,reallocated,4,{});check(!b.view_motion.count(90),"allocation generation reused history");
    evrm::NativePreviewStamp first_preview;first_preview.subject=evrm::NativePreviewSubject::local;first_preview.request=1;first_preview.source.epoch=5;
    b.bind_motion(90,{},4,first_preview);b.motion(90).contact.owner=42;
    auto next_preview=first_preview;next_preview.request=2;b.bind_motion(90,{},4,next_preview);
    check(b.view_motion.count(90)&&b.view_motion.at(90).contact.owner==42,"fresh preview request discarded the same actor history");
    ++next_preview.source.world_lifetime;b.bind_motion(90,{},4,next_preview);check(!b.view_motion.count(90),"preview lifetime reused actor history");
    const auto rejected=[&](std::shared_ptr<const evrm::NativePoseOwner> proposed){try{b.build_native_view(snapshot,8,9,bridge,voice,1,0,timings,nullptr,nullptr,proposed,nullptr,"remote",remote,8);}catch(const std::exception&){return true;}return false;};
    check(rejected({}),"remote used unowned menu fallback");
    auto wrong=std::make_shared<evrm::NativePoseOwner>(*remote_owner);++wrong->candidate.account;check(rejected(wrong),"wrong current account accepted");
    snapshot.preview_subject.source.epoch=5;check(rejected(remote_owner),"remote admitted to local preview");snapshot.preview_subject.source.epoch=0;
    auto other_asset=std::make_shared<vrm::Avatar>(*asset);other_asset->nodes[6].local[13]+=1;worlds(*other_asset);
    auto other_model=std::make_shared<evrm::PreparedAvatar>();other_model->asset=other_asset;evrm::AvatarCpuModel distinct(other_model,true,true);
    auto different=distinct.build_native_view(snapshot,8,9,bridge,voice,1,0,timings,nullptr,nullptr,remote_owner,nullptr,"remote",remote,8);
    check(different->asset!=pa->asset&&different->skinning->asset!=pa->skinning->asset,"distinct models reused one asset");
    auto surface_asset=std::make_shared<vrm::Avatar>(*asset);surface_asset->mouth.reset();surface_asset->materials.resize(1);
    auto normal=std::make_shared<vrm::MaterialSurface>();normal->normal.texture=0;surface_asset->materials[0].surface=normal;
    auto& surface_primitive=surface_asset->meshes[0].primitives.back();surface_primitive.material=0;surface_primitive.surface_vertices.resize(surface_primitive.vertices.size());
    for(auto& v:surface_primitive.surface_vertices){v.normal_uv[0]=.25f;v.tangent[0]=1;v.tangent[3]=1;}
    auto surface_model=std::make_shared<evrm::PreparedAvatar>();surface_model->asset=surface_asset;evrm::AvatarCpuModel cpu_surface(surface_model,true,false);
    auto fast=std::make_shared<evrm::AvatarAppearance>();fast->whole=evrm::AvatarShaderPreset::Fast;
    auto enhanced=std::make_shared<evrm::AvatarAppearance>();enhanced->whole=evrm::AvatarShaderPreset::Enhanced;
    const auto cpu_view=[&](const std::shared_ptr<const evrm::AvatarAppearance>& style){return cpu_surface.build_native_view(snapshot,10,11,bridge,{},0,0,timings,nullptr,nullptr,remote_owner,nullptr,"remote",remote,8,style);};
    auto cpu_fast=cpu_view(fast),cpu_enhanced=cpu_view(enhanced);
    check(!cpu_fast->skinning&&!cpu_enhanced->skinning&&cpu_fast->geometry->surface_vertices.empty(),"CPU Fast allocated optional surface stream");
    check(cpu_enhanced->geometry->surface_vertices.size()==cpu_enhanced->geometry->vertices.size()&&!cpu_enhanced->geometry->surface_vertices.empty(),"CPU Enhanced fallback lost parallel surface stream");
    check(cpu_enhanced->subject=="remote"&&cpu_enhanced->assignment_generation==8&&cpu_enhanced->asset==surface_asset,"CPU surface fallback changed assignment");
    snapshot.buffers.at("instances").bytes.resize(17*128);snapshot.buffers.at("instances").binding.range=17*128;
    std::vector<std::unique_ptr<evrm::AvatarCpuModel>> benchmark_models;std::vector<std::shared_ptr<evrm::NativePoseOwner>> benchmark_owners;
    for(unsigned n=1;n<=16;++n){benchmark_models.push_back(std::make_unique<evrm::AvatarCpuModel>(model,true,true));benchmark_owners.push_back(owner(n));
        auto f=evrm::fixture_frames.at(1);f.parts[0].instance_id=n;f.source_pose.current.at("hips")[12]=float(n)*.01f;evrm::fixture_frames[n]=f;
        benchmark_models.back()->build_native_view(snapshot,20,20,bridge,{},0,0,timings,nullptr,nullptr,benchmark_owners.back(),nullptr,n==1?"":"p"+std::to_string(n),{100+n,n,64+n,n==1},1);
    }
    for(unsigned count:{1u,2u,16u}){const auto began=std::chrono::steady_clock::now();
        for(unsigned sample=0;sample<64;++sample){snapshot.captured_milliseconds=200+sample;
            for(unsigned n=1;n<=count;++n)benchmark_models[n-1]->build_native_view(snapshot,30+sample,30+sample,bridge,{},0,0,timings,nullptr,nullptr,benchmark_owners[n-1],nullptr,n==1?"":"p"+std::to_string(n),{100+n,n,64+n,n==1},1);
        }
        std::cout<<"CPU fixture warm us/view subjects="<<count<<" value="<<std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count()/64<<'\n';
    }
    std::cout<<"PASS actual CPU native builds: shared capture, distinct/shared assets and poses, independent retarget/contact, local-only voice/report/trace, actor/allocation/source invalidation, remote preview/owner refusal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
#ifdef EVRM_PLAYER_RENDERER_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/avatar_service.hpp"
#include "../src/avatar_switch_transaction.hpp"
#include <map>
#include <mutex>
#include <iostream>
#include <stdexcept>
#if __has_include("player_renderer_methods.inc")
namespace evrm {
struct AvatarCpuModel {std::shared_ptr<const PreparedAvatar> prepared;AvatarCpuModel(std::shared_ptr<const PreparedAvatar> p,bool,bool):prepared(std::move(p)){if(prepared->name=="fail")throw std::runtime_error("fixture failure");}};
struct AvatarRenderer {
    struct PlayerModelState {std::unique_ptr<AvatarCpuModel> model,staged_model;AvatarCandidatePtr staged;std::string staged_failure;bool staged_gpu_ready=false;std::shared_ptr<const NativeAvatarPublication> staged_publication;};
    struct Impl {std::mutex resource_mutex;bool native_mode=true,gpu_skinning=true,native_replacement=true,active=true;std::map<std::string,PlayerModelState> players;std::shared_ptr<const NativeAvatarPublication> publication;
        std::unique_ptr<AvatarCpuModel> model,staged_model;AvatarCandidatePtr staged;std::string staged_failure;bool staged_gpu_ready=false;std::shared_ptr<const NativeAvatarPublication> staged_publication;};
    std::unique_ptr<Impl> impl=std::make_unique<Impl>();
    std::shared_ptr<const PreparedAvatar> prepared_model()const noexcept{return {};}
    bool stage_candidate(const AvatarCandidatePtr&,bool,std::string&)noexcept{return false;}
    bool commit_candidate(const AvatarCandidate&,std::uint64_t)noexcept{return false;}
    std::shared_ptr<const PreparedAvatar> prepared_player_model(const std::string&)const noexcept;
    bool stage_player_candidate(const AvatarCandidatePtr&,bool,std::string&)noexcept;
    bool commit_player_candidate(const AvatarCandidate&,std::uint64_t)noexcept;
    void retain_player_models(const std::vector<AvatarSubjectSelection>&)noexcept;
};
#include "player_renderer_methods.inc"
}
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    evrm::AvatarRenderer renderer;auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=std::make_shared<vrm::Avatar>();
    auto skin=std::make_shared<evrm::NativeSkinAsset>();skin->layout=std::make_shared<evrm::NativeAvatarPose>();model->skin=skin;
    auto a=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{10,model,"a"});auto b=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{11,model,"b"});std::string error;
    require(renderer.stage_player_candidate(a,true,error)&&renderer.stage_player_candidate(b,true,error),"independent staging failed");
    auto& players=renderer.impl->players;require(players.at("a").staged_model.get()!=players.at("b").staged_model.get(),"mutable CPU models shared");
    const auto proof=[&](const evrm::AvatarCandidate& candidate){auto p=std::make_shared<evrm::NativeAvatarPublication>();p->subject=candidate.subject;p->assignment_generation=candidate.generation;p->asset=model->asset;p->geometry=skin->layout;auto dynamic=std::make_shared<evrm::NativeSkinPose>();dynamic->asset=skin;p->skinning=dynamic;p->source_instances=90;p->source_buffer_epoch=3;p->serial=1;p->published_milliseconds=GetTickCount64();return p;};
    auto pa=proof(*a),pb=proof(*b);players.at("a").staged_publication=pa;players.at("b").staged_publication=pb;
    pa->subject="b";require(!renderer.commit_player_candidate(*a,3),"wrong-subject pose committed");pa->subject="a";
    pa->assignment_generation=9;require(!renderer.commit_player_candidate(*a,3),"old assignment pose committed");pa->assignment_generation=10;
    require(renderer.commit_player_candidate(*a,3)&&renderer.prepared_player_model("a")==model&&!renderer.prepared_player_model("b"),"target commit changed unrelated player");
    require(players.at("b").staged==b&&renderer.commit_player_candidate(*b,3),"second player staging lost");
    require(players.at("a").model.get()!=players.at("b").model.get(),"shared asset shares mutable state");
    require(renderer.stage_player_candidate(a,false,error)&&!renderer.commit_player_candidate(*a,3)&&renderer.prepared_player_model("b")==model,"pending upload changed unrelated player");
    evrm::AvatarRenderer retained;auto& r=*retained.impl;
    std::weak_ptr<const evrm::PreparedAvatar> old_model,old_stage,local_stage;
    std::weak_ptr<const vrm::Avatar> old_asset;
    evrm::AvatarSubjectSelection live;live.subject="live";live.replacement_enabled=true;live.identity={300,3,67,false};live.model=model;
    live.selection=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{30,model,"live"});
    r.players["live"].model=std::make_unique<evrm::AvatarCpuModel>(model,true,true);auto* live_cpu=r.players.at("live").model.get();
    {auto old=std::make_shared<evrm::PreparedAvatar>();old->asset=std::make_shared<vrm::Avatar>();old_model=old;old_asset=old->asset;
        auto pending=std::make_shared<evrm::PreparedAvatar>();old_stage=pending;
        auto& state=r.players["disabled"];state.model=std::make_unique<evrm::AvatarCpuModel>(old,true,true);state.staged_model=std::make_unique<evrm::AvatarCpuModel>(pending,true,true);state.staged=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{20,pending,"disabled"});
        auto first=std::make_shared<evrm::NativeAvatarPublication>();first->subject="disabled";first->asset=old->asset;first->source_instances=90;first->serial=7;first->presentation_serial=8;first->published_milliseconds=100;
        auto eligible=std::make_shared<evrm::NativeAvatarPublication>(*first);eligible->subject="live";eligible->asset=model->asset;eligible->player_identity=live.identity;eligible->assignment_generation=30;first->additional_subjects.push_back(eligible);
        auto view=std::make_shared<evrm::NativeAvatarPublication>(*first);view->additional_views.clear();view->source_instances=91;view->additional_subjects.clear();auto child=std::make_shared<evrm::NativeAvatarPublication>(*eligible);child->source_instances=91;view->additional_subjects.push_back(child);first->additional_views.push_back(view);r.publication=first;
        auto pending_local=std::make_shared<evrm::PreparedAvatar>();local_stage=pending_local;r.staged_model=std::make_unique<evrm::AvatarCpuModel>(pending_local,true,true);r.staged=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{40,pending_local,""});r.staged_publication=first;
    }
    evrm::AvatarSubjectSelection local_off;local_off.subject="";r.model=std::make_unique<evrm::AvatarCpuModel>(model,true,true);auto* local_cpu=r.model.get();
    retained.retain_player_models({local_off,live});
    require(old_model.expired()&&old_stage.expired()&&old_asset.expired()&&local_stage.expired(),"disabled remote/cancelled staging retains assets without another pose read");
    require(r.players.size()==1&&r.players.at("live").model.get()==live_cpu&&r.model.get()==local_cpu,"cleanup changed unrelated live CPU state or bounded local cache");
    require(r.publication&&r.publication->subject=="live"&&r.publication->additional_subjects.empty()&&r.publication->additional_views.size()==1&&r.publication->additional_views[0]->subject=="live","eligible sibling was not promoted across retained views");
    require(r.publication->serial==7&&r.publication->presentation_serial==8&&r.publication->published_milliseconds==100,"cleanup renewed a retained cohort");
    auto reassigned=live;reassigned.selection=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{31,model,"live"});retained.retain_player_models({local_off,reassigned});require(!r.publication&&r.players.at("live").model.get()==live_cpu,"reassigned publication survived or unrelated active model was lost");
    retained.retain_player_models({local_off});require(r.players.empty()&&!r.publication&&r.model.get()==local_cpu,"all-None retains remote resources or erased local compatibility cache");
    evrm::AvatarRenderer all_none;std::weak_ptr<const evrm::PreparedAvatar> none_model;std::weak_ptr<const vrm::Avatar> none_asset;std::weak_ptr<const evrm::NativeAvatarPublication> none_publication;
    {auto asset_model=std::make_shared<evrm::PreparedAvatar>();asset_model->asset=std::make_shared<vrm::Avatar>();none_model=asset_model;none_asset=asset_model->asset;
        auto& state=all_none.impl->players["off"];state.model=std::make_unique<evrm::AvatarCpuModel>(asset_model,true,true);state.staged_model=std::make_unique<evrm::AvatarCpuModel>(asset_model,true,true);state.staged=std::make_shared<const evrm::AvatarCandidate>(evrm::AvatarCandidate{50,asset_model,"off"});
        auto publication=std::make_shared<evrm::NativeAvatarPublication>();publication->subject="off";publication->asset=asset_model->asset;none_publication=publication;all_none.impl->publication=publication;state.staged_publication=publication;
    }
    evrm::AvatarSubjectSelection remote_off;remote_off.subject="off";all_none.retain_player_models({local_off,remote_off});
    require(none_model.expired()&&none_asset.expired()&&none_publication.expired()&&all_none.impl->players.empty(),"all-None retained remote CPU/model/staged/publication references");
    std::cout<<"PASS actual per-player renderer methods: isolated staging/activation, independent mutable models, subject/generation/epoch pose proof and readback-free None/disconnect/reassignment pruning\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){std::cerr<<"Missing per-player renderer methods\n";return 1;}
#endif
#else
#include "../src/native_publication.hpp"
#include "../src/avatar_switch_transaction.hpp"
#include <iostream>
#include <stdexcept>
#if __has_include("../src/native_player_publication.hpp")
#include "../src/native_player_publication.hpp"
using namespace evrm;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::shared_ptr<NativeAvatarPublication> leaf(std::string subject,unsigned part,unsigned asset=1){
    auto p=std::make_shared<NativeAvatarPublication>();p->subject=std::move(subject);p->assignment_generation=7;
    p->player_identity={100+part,part,part*64,false};p->asset=std::make_shared<vrm::Avatar>();
    (void)asset;p->source_instances=90;p->source_buffer_epoch=3;p->serial=8;p->presentation_serial=9;p->published_milliseconds=100;
    p->parts.push_back({part,part,0,0});return p;
}
int main(){try{
    auto local=leaf("",1),remote=leaf("remote-a",2);local->additional_subjects.push_back(remote);
    require(native_publication_subjects_valid(local),"two assets in same view rejected");
    require(native_publication_subject(local,"remote-a")==remote,"subject lookup failed");
    require(native_publication_view(local,90)==local,"subjects counted as views");
    auto shared=leaf("remote-b",3);shared->asset=remote->asset;local->additional_subjects.push_back(shared);
    require(native_publication_subjects_valid(local),"shared immutable asset rejected");
    auto duplicate=std::make_shared<NativeAvatarPublication>(*shared);duplicate->subject=remote->subject;local->additional_subjects.push_back(duplicate);
    require(!native_publication_subjects_valid(local),"duplicate subject accepted");local->additional_subjects.pop_back();
    duplicate=std::make_shared<NativeAvatarPublication>(*shared);duplicate->subject="overlap";duplicate->parts=remote->parts;local->additional_subjects.push_back(duplicate);
    require(!native_publication_subjects_valid(local),"overlapping ownership accepted");local->additional_subjects.pop_back();
    auto held=hold_native_publication(local,3,350,10);
    require(held&&held->additional_subjects.size()==2&&held->published_milliseconds==100&&held->additional_subjects[0]->published_milliseconds==100,"held timestamps renewed or subjects lost");
    require(held->serial==10&&held->additional_subjects[0]->serial==10,"cohort serial diverged");
    require(!hold_native_publication(held,3,351,11),"expired group renewed");
    require(!hold_native_publication(local,4,110,10),"changed source epoch held");
    auto appearance=std::make_shared<AvatarAppearance>();auto changed=native_publication_appearance(local,appearance);
    require(changed&&changed->appearance==appearance&&changed->additional_subjects[0]->appearance!=appearance,"appearance applied to another asset");
    auto replacement=leaf("remote-a",2);replacement->assignment_generation=8;
    auto switched=replace_native_publication_subject(local,replacement);
    require(switched&&native_publication_subject(switched,"remote-a")->asset==replacement->asset&&native_publication_subject(switched,"")->asset==local->asset&&native_publication_subject(switched,"remote-b")->asset==shared->asset,"target commit lost unrelated publications");
    replacement->serial=99;require(!replace_native_publication_subject(local,replacement),"incompatible cohorts merged");replacement->serial=8;
    require(replace_native_publication_subject({},replacement)==replacement,"first remote-only activation unavailable");
    auto only_remote=leaf("remote-only",4);require(native_publication_subjects_valid(only_remote),"remote-only root rejected");
    for(unsigned n=5;n<20;++n)only_remote->additional_subjects.push_back(leaf("p"+std::to_string(n),n));
    require(native_publication_subjects_valid(only_remote),"16-subject budget rejected");
    only_remote->additional_subjects.push_back(leaf("too-many",20));require(!native_publication_subjects_valid(only_remote),"17 subjects accepted");only_remote->additional_subjects.pop_back();
    for(unsigned n=0;n<3;++n){auto view=std::make_shared<NativeAvatarPublication>(*only_remote);view->additional_views.clear();view->source_instances=91+n;for(auto& child:view->additional_subjects){auto copy=std::make_shared<NativeAvatarPublication>(*child);copy->source_instances=view->source_instances;child=copy;}only_remote->additional_views.push_back(view);}
    require(native_publication_view(only_remote,93)&&hold_native_publication(only_remote,3,110,10),"independent view/subject budgets rejected");
    auto nested=std::make_shared<NativeAvatarPublication>(*remote);nested->additional_subjects.push_back(shared);local->additional_subjects[0]=nested;
    require(!native_publication_subjects_valid(local)&&!hold_native_publication(local,3,110,10),"nested subject accepted");
    std::cout<<"PASS per-player publication: distinct/shared assets, remote-only, disjoint masks, bounded views/subjects, unchanged held age and cohort serial, appearance isolation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){std::cerr<<"Missing bounded subject publication support\n";return 1;}
#endif

#endif

#endif
