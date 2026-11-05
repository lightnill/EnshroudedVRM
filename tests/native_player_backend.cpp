// Preload the standard library before this fixture exposes private members.
// GCC's bits/stdc++.h is not a portable standard-library header.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#define private public
#include "../src/native_backend.hpp"
#undef private
#include "../src/native_backend.cpp"
#include "../src/native_player_pose_owner.hpp"
#include <type_traits>
#include <iostream>
#include <exception>
#include <cstdlib>
#include <csignal>
#include <windows.h>
std::shared_ptr<const std::vector<evrm::AvatarSubjectSelection>> fixture_runtime_subjects;
namespace evrm {std::shared_ptr<const std::vector<AvatarSubjectSelection>> avatar_runtime_subjects()noexcept{return fixture_runtime_subjects;}
bool debug_report_sources_pending()noexcept{return false;} void debug_report_observe_lighting(std::array<std::uint32_t,3>)noexcept{}}
#include "../src/standalone_startup.hpp"
#include "../vendor/minhook/include/MinHook.h"
extern "C" MH_STATUS WINAPI MH_Initialize(){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_ApplyQueued(){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_ERROR_NOT_INITIALIZED;}
namespace evrm {const RuntimeStartupContext& runtime_startup_context()noexcept{static RuntimeStartupContext c;return c;} bool write_native_visual_slot_data(std::uint64_t,const float*,std::size_t)noexcept{return false;}}
template<class T,class=void>struct has_players:std::false_type{};
template<class T>struct has_players<T,std::void_t<decltype(&T::player_pose_owners),decltype(&T::prepare_player_model),decltype(&T::activate_player_model),decltype(&T::retain_player_models)>>:std::true_type{};
template<class T,class=void>struct has_group:std::false_type{};
template<class T>struct has_group<T,std::void_t<decltype(&T::prepare_player_group)>>:std::true_type{};
#include "native_player_fake_vulkan.hpp"
std::filesystem::path fixture_shaders;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class Backend,class B=evrm::Bundle>void group_cases(){
 if constexpr(has_group<Backend>::value){
  Backend backend;auto& c=backend.context;c.device=fake_player_gpu::handle<VkDevice>(1);c.get=fake_player_gpu::get;c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  backend.shader_directory=fixture_shaders;
  auto make=[&](unsigned bit){auto b=std::make_shared<B>(c);b->frame=evrm::make_buffer(c,sizeof(evrm::NativePreparedFrame),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);b->control=evrm::make_buffer(c,sizeof(evrm::NativePrepareControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);b->body_bits=evrm::make_buffer(c,sizeof(b->body_mask.words),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);b->body_mask.words[0]=bit;return b;};
  evrm::Recording r;auto a=make(2),b=make(8);require(backend.prepare_player_group(r,{a,b}),"disjoint subjects declined");
  require(r.bundle==a&&a->subjects.size()==1&&a->subjects[0]==b,"subject group lost a replacement");
  require(*static_cast<unsigned*>(a->body_bits->mapped)==10,"native suppression must be union of only included subjects");
  require(a->body_mask.words[0]==2&&b->body_mask.words[0]==8,"union destroyed per-owner revalidation masks");
  auto* af=reinterpret_cast<unsigned*>(static_cast<unsigned char*>(a->frame->mapped)+192);auto* bf=reinterpret_cast<unsigned*>(static_cast<unsigned char*>(b->frame->mapped)+192);
  *af=1;*bf=0;*static_cast<unsigned*>(a->common_gate->mapped)=1;
  require(backend.player_commit->record(fake_player_gpu::handle<VkCommandBuffer>(2),*a->common_commit),"common commit record declined");
  require(!*af&&!*bf,"one GPU-declined subject left replacement or clipping enabled");
  *af=*bf=1;*static_cast<unsigned*>(a->common_gate->mapped)=1;backend.player_commit->record(fake_player_gpu::handle<VkCommandBuffer>(2),*a->common_commit);require(*af==1&&*bf==1,"common GPU success lost a subject");
  std::vector<std::shared_ptr<B>> sixteen;for(unsigned i=0;i<16;++i)sixteen.push_back(make(1u<<i));evrm::Recording full;require(backend.prepare_player_group(full,sixteen)&&full.bundle->subjects.size()==15&&*static_cast<unsigned*>(full.bundle->body_bits->mapped)==65535,"proven sixteen-player bound did not prepare complete union");
  for(const auto& item:sixteen)*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(item->frame->mapped)+192)=1;
  *reinterpret_cast<unsigned*>(static_cast<unsigned char*>(sixteen.back()->frame->mapped)+192)=0;*static_cast<unsigned*>(full.bundle->common_gate->mapped)=1;backend.player_commit->record(fake_player_gpu::handle<VkCommandBuffer>(3),*full.bundle->common_commit);
  for(const auto& item:sixteen)require(!*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(item->frame->mapped)+192),"sixteenth GPU failure escaped group reduction");
  auto over=sixteen;over.push_back(make(1u<<16));evrm::Recording oversized;require(!backend.prepare_player_group(oversized,over),"seventeenth subject exceeded bound");
  evrm::Recording overlap;require(!backend.prepare_player_group(overlap,{make(2),make(2)})&&!overlap.bundle,"overlapping owners gained suppression");
  evrm::Recording single;auto remote=make(8);require(backend.prepare_player_group(single,{remote}),"local None prevented remote-only group");require(*static_cast<unsigned*>(remote->body_bits->mapped)==8,"unassigned subject was suppressed");
 }
}

std::unique_ptr<evrm::NativeBackend> backend_fixture(){
 using namespace fake_player_gpu;auto out=std::make_unique<evrm::NativeBackend>(handle<VkInstance>(1),handle<VkPhysicalDevice>(2),handle<VkDevice>(3),instance_get,get,nullptr,fixture_shaders,true,false);
 auto& b=*out->impl;b.initialized=true;b.limits.maxImageDimension2D=16384;b.asset_stager=std::make_unique<evrm::NativeAvatarAssetStager>(b.context.device,b.context.get,nullptr,b.context.memory,b.limits.maxImageDimension2D,nullptr,&b.context.uncertain);return out;
}
bool accept(void*,std::uint64_t)noexcept{return true;}
bool reject(void*,std::uint64_t)noexcept{return false;}
evrm::AvatarSubjectSelection selection(std::string subject,unsigned gen,std::shared_ptr<const evrm::PreparedAvatar> model){
 evrm::AvatarSubjectSelection s;s.subject=subject;s.replacement_enabled=true;s.model=model;s.desired_generation=gen;
 s.candidate=std::make_shared<evrm::AvatarCandidate>(evrm::AvatarCandidate{gen,model,subject});return s;
}
void staged_ready_cases(){
 using namespace fake_player_gpu;auto api=backend_fixture();const auto queue=handle<VkQueue>(4);
 auto a=std::make_shared<evrm::PreparedAvatar>();a->asset=std::make_shared<vrm::Avatar>();auto b=std::make_shared<evrm::PreparedAvatar>();b->asset=std::make_shared<vrm::Avatar>();
 auto offscreen=selection("fixture-offscreen",1,a),visible=selection("fixture-visible",2,b);api->retain_player_models({offscreen,visible});
 require(api->prepare_player_model(queue,0,offscreen.candidate)==evrm::NativeAvatarUploadState::Pending,"offscreen upload start");
 require(api->prepare_player_model(queue,0,offscreen.candidate)==evrm::NativeAvatarUploadState::Ready&&!api->activate_player_model(*offscreen.candidate,reject,nullptr),"offscreen must await pose authorization");
 const auto before=submits;require(api->prepare_player_model(queue,0,visible.candidate)==evrm::NativeAvatarUploadState::Pending&&submits==before+1,"ready offscreen subject blocked independent upload");
 require(api->prepare_player_model(queue,0,offscreen.candidate)==evrm::NativeAvatarUploadState::Ready,"another upload lost staged readiness");
 auto shared=selection("fixture-staged-shared",3,a);api->retain_player_models({offscreen,visible,shared});const auto sharing_uploads=submits;
 require(api->prepare_player_model(queue,0,shared.candidate)==evrm::NativeAvatarUploadState::Ready&&submits==sharing_uploads,"same-model ready subjects did not share staged assets during unrelated upload");
 std::weak_ptr<evrm::NativeAvatarGpuAssets> staged_assets=api->impl->player_models.at(offscreen.subject).ready;
 require(api->prepare_player_model(queue,0,visible.candidate)==evrm::NativeAvatarUploadState::Ready&&api->activate_player_model(*visible.candidate,accept,nullptr),"visible activation blocked by offscreen readiness");
 visible.selection=visible.candidate;visible.candidate.reset();offscreen.replacement_enabled=false;api->retain_player_models({offscreen,visible,shared});
 require(!api->activate_player_model(*offscreen.candidate,accept,nullptr)&&api->impl->player_models.at(visible.subject).assets,"None retained offscreen readiness or removed visible assets");
 require(!staged_assets.expired()&&api->activate_player_model(*shared.candidate,accept,nullptr),"cancelling original ready target destroyed another target's shared lease");
 auto stale=*visible.selection;++stale.generation;require(api->player_model_ready(*visible.selection)&&!api->player_model_ready(stale),"device readiness ignored assignment generation");
 shared.selection=shared.candidate;shared.candidate.reset();visible.replacement_enabled=false;api->retain_player_models({offscreen,visible,shared});require(api->player_model_ready(*shared.selection)&&!api->player_model_ready(*visible.selection),"B None affected independent shared activation");
 api->retain_player_models({});require(staged_assets.expired()&&api->impl->player_models.empty(),"disconnect retained staged or active asset leases");
 std::cout<<"PASS completed uploads wait independently for per-player pose authorization\n";
}
void asset_cases(){
 using namespace fake_player_gpu;auto api=backend_fixture();auto& b=*api->impl;const auto queue=handle<VkQueue>(4);
 auto first=std::make_shared<evrm::PreparedAvatar>();first->asset=std::make_shared<vrm::Avatar>();auto second=std::make_shared<evrm::PreparedAvatar>();second->asset=std::make_shared<vrm::Avatar>();
 auto local=selection({},1,first),remote=selection("fixture-remote",2,second);api->retain_player_models({local,remote});pending=true;
 require(api->prepare_player_model(queue,0,local.candidate)==evrm::NativeAvatarUploadState::Pending,"first upload not pending");const auto started=submits;
 require(api->prepare_player_model(queue,0,remote.candidate)==evrm::NativeAvatarUploadState::Pending&&submits==started,"second target started concurrent upload");
 api->prepare_player_model(queue,0,{});pending=false;
 require(api->prepare_player_model(queue,0,local.candidate)==evrm::NativeAvatarUploadState::Ready,"empty request cancelled unrelated pending upload");
 require(!api->activate_player_model(*local.candidate,reject,nullptr)&&!b.player_models.at({}).assets,"rejected transaction activated GPU model");
 require(api->activate_player_model(*local.candidate,accept,nullptr),"local activation failed");local.selection=local.candidate;local.candidate.reset();api->retain_player_models({local,remote});
 const auto local_asset=b.player_models.at({}).assets;
 require(api->prepare_player_model(queue,0,remote.candidate)==evrm::NativeAvatarUploadState::Pending,"remote upload not pending");require(b.player_models.at({}).assets==local_asset,"remote upload replaced local asset");
 require(api->prepare_player_model(queue,0,remote.candidate)==evrm::NativeAvatarUploadState::Ready&&api->activate_player_model(*remote.candidate,accept,nullptr),"remote activation failed");
 remote.selection=remote.candidate;remote.candidate.reset();api->retain_player_models({local,remote});require(b.player_models.at({}).assets==local_asset&&b.player_models.at(remote.subject).assets!=local_asset,"distinct model assets not isolated");
 auto shared=selection("fixture-shared",3,first);api->retain_player_models({local,remote,shared});const auto before=submits;
 require(api->prepare_player_model(queue,0,shared.candidate)==evrm::NativeAvatarUploadState::Ready&&submits==before,"same prepared model uploaded twice");require(api->activate_player_model(*shared.candidate,accept,nullptr)&&b.player_models.at(shared.subject).assets==local_asset,"shared model lost immutable asset sharing");
 std::cout<<"METRIC enabled-assignments=3 unique-GPU-model-assets=2 shared-upload-count=0\n";
 shared.selection=shared.candidate;shared.candidate.reset();
 auto held=std::make_shared<evrm::Bundle>(b.context);held->assets=b.player_models.at(remote.subject).assets;std::weak_ptr<evrm::NativeAvatarGpuAssets> retiring=held->assets;
 remote.replacement_enabled=false;api->retain_player_models({local,remote,shared});require(!b.player_models.count(remote.subject)&&!retiring.expired(),"None freed command-held assets or retained warm assignment");held.reset();require(retiring.expired(),"command retirement leaked disabled remote assets");
 // Disappearing local work must not cancel a remote job that is still current.
 auto pending_remote=selection("fixture-upload",4,second);api->retain_player_models({local,shared,pending_remote});pending=true;
 require(api->prepare_player_model(queue,0,pending_remote.candidate)==evrm::NativeAvatarUploadState::Pending,"remote retry did not start");local.replacement_enabled=false;api->retain_player_models({local,shared,pending_remote});pending=false;
 require(api->prepare_player_model(queue,0,pending_remote.candidate)==evrm::NativeAvatarUploadState::Ready,"local None cancelled remote upload");
 pending_remote.replacement_enabled=false;const auto destroyed=images_destroyed;api->retain_player_models({local,shared,pending_remote});require(!b.player_upload&&!b.player_models.count(pending_remote.subject)&&images_destroyed>destroyed,"disabled ready upload leaked");
 auto other_skin=std::make_shared<evrm::PreparedAvatar>();other_skin->asset=first->asset;other_skin->skin=std::make_shared<evrm::NativeSkinAsset>();auto skin_request=selection("fixture-skin",6,other_skin);api->retain_player_models({local,shared,skin_request});
 require(api->prepare_player_model(queue,0,skin_request.candidate)!=evrm::NativeAvatarUploadState::Ready,"different prepared skin reused incompatible immutable GPU assets");
 auto changed=selection({},5,second);api->retain_player_models({changed,shared});const auto shared_assets=b.player_models.at(shared.subject).assets;
 require(api->prepare_model(queue,0,changed.candidate)==evrm::NativeAvatarUploadState::Pending,"legacy local switch not pending");require(api->prepare_model(queue,0,changed.candidate)==evrm::NativeAvatarUploadState::Ready&&api->activate_model(*changed.candidate,accept,nullptr),"legacy local switch failed");
 require(b.player_models.count({})&&b.player_models.at({}).assets&&b.player_models.at({}).assets->avatar()==second->asset&&b.player_models.at(shared.subject).assets==shared_assets,"legacy local activation bypassed per-target state");
 std::cout<<"PASS actual backend serialized uploads, isolated activation, shared assets and None retirement\n";
}
void iteration_history_cases(){
 auto api=backend_fixture();auto& b=*api->impl;auto a=std::make_shared<evrm::Bundle>(b.context),z=std::make_shared<evrm::Bundle>(b.context);a->subjects={z};evrm::Recording r;r.bundle=a;r.first_indirect=fake_player_gpu::handle<VkBuffer>(45);r.first_count=fake_player_gpu::handle<VkBuffer>(46);
 unsigned draws=0;b.each_subject(r,[&](evrm::Recording& s){require(!s.pass_drawn&&s.first_indirect==fake_player_gpu::handle<VkBuffer>(45),"sibling lost original draw witness");s.first_indirect={};s.first_count={};s.pass_drawn=true;++draws;});require(draws==2&&r.bundle==a&&a->pass_drawn&&z->pass_drawn&&!r.first_indirect,"draw flags lost or root not restored");
 std::vector<const evrm::Bundle*> order;b.each_subject(r,[&](evrm::Recording& current){order.push_back(current.bundle.get());},true);require(order.size()==2&&order[0]==z.get()&&order[1]==a.get(),"shadow union must bind only after every subject caster");
 try{b.each_subject(r,[&](evrm::Recording& current){if(current.bundle==z)throw std::runtime_error("fixture decline");},true);}catch(const std::runtime_error&){}require(r.bundle==a&&r.tainted,"failed sibling recording lost root resource leases");
 evrm::NativeAvatarPublication local,remote;local.assignment_generation=1;remote.subject="fixture-remote";remote.assignment_generation=2;
 const auto source=fake_player_gpu::handle<VkBuffer>(50);const VkExtent2D extent{1920,1080};const auto k1=b.history_key(source,extent,&local),k2=b.history_key(source,extent,&remote);
 require(k1!=k2,"two actors share one motion-history key");auto x=std::make_shared<evrm::SubmittedHistory>(),y=std::make_shared<evrm::SubmittedHistory>();b.update_history(k1,x);b.update_history(k2,y);require(b.history_for(source,extent,&local)==x&&b.history_for(source,extent,&remote)==y,"same native view collapsed independent histories");
 auto old=k2;remote.assignment_generation=3;require(old!=b.history_key(source,extent,&remote),"reassignment retained predecessor history");
 for(unsigned i=0;i<70;++i){remote.assignment_generation=i+3;b.update_history(b.history_key(source,extent,&remote),y);}require(std::atomic_load(&b.submitted_histories)->size()==64,"subject/view history bank is not bounded at 64");
 std::cout<<"PASS actual subject draw witnesses and bounded independent histories\n";
}

#include "native_player_backend_owner_fixture.hpp"
struct NativeSources {evrm::NativeBufferBinding instances,arena;};
NativeSources native_sources(evrm::NativeBackend::Impl& b){
 using namespace fake_player_gpu;auto& m=b.metadata;
 const auto source=handle<VkBuffer>(0x12340000),arena=handle<VkBuffer>(0x12350000);
 for(const auto pair:{std::pair<VkBuffer,unsigned>{source,8192},std::pair<VkBuffer,unsigned>{arena,65536}}){VkBufferCreateInfo i{};i.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;i.size=pair.second;i.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;const auto memory_handle=handle<VkDeviceMemory>(reinterpret_cast<std::uintptr_t>(pair.first)+1);VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=i.size;m.buffer_created(pair.first,i);b.memory.buffer_created(pair.first,i);b.memory.memory_created(memory_handle,a);b.memory.bound(pair.first,memory_handle,0);}
 const auto dsl=handle<VkDescriptorSetLayout>(0x600);const auto pl=handle<VkPipelineLayout>(0x601);const auto set=handle<VkDescriptorSet>(0x602);const auto pool=handle<VkDescriptorPool>(0x603);const auto shader_handle=handle<VkShaderModule>(0x604);const auto pipeline=handle<VkPipeline>(0x605);const auto cmd=handle<VkCommandBuffer>(0x606);
 const std::array<VkDescriptorSetLayoutBinding,2> bindings{{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};VkDescriptorSetLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;li.bindingCount=2;li.pBindings=bindings.data();m.layout_created(dsl,li);
 VkPipelineLayoutCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;pi.setLayoutCount=1;pi.pSetLayouts=&dsl;m.pipeline_layout_created(pl,pi);
 VkDescriptorSetAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&dsl;m.sets_allocated(ai,&set);
 const VkDescriptorBufferInfo source_info{source,0,8192},arena_info{arena,0,65536};std::array<VkWriteDescriptorSet,2> writes{};for(unsigned n=0;n<2;++n){writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=set;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=bindings[n].descriptorType;writes[n].pBufferInfo=n?&arena_info:&source_info;}m.descriptors_written(2,writes.data());
 const std::array<unsigned,5> words{0x07230203,0x10000,0,1,0};VkShaderModuleCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;si.codeSize=20;si.pCode=words.data();m.shader_created(shader_handle,si);
 VkComputePipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;ci.layout=pl;ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=shader_handle;ci.stage.pName="main";m.compute_pipelines_created(1,&ci,&pipeline);m.command_begin(cmd);m.pipeline_bound(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);m.sets_bound(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,nullptr);
 const auto source_bound=m.observed_bound(cmd,VK_PIPELINE_BIND_POINT_COMPUTE);NativeSources out{m.observed_buffer_binding(source_bound,0,0),m.observed_buffer_binding(source_bound,0,1)};require(m.live(out.instances)&&m.live(out.arena),"fixture native descriptor/source certificates not current");return out;
}
std::size_t backend_actor_slot(const Fixture&,unsigned);
void backend_absent_actor(Fixture&,unsigned,bool);
void submission_cases(){
 using namespace fake_player_gpu;BackendPlayersFixture owned(3);owned.memory.batched=true;evrm::NativeOwnerLimits limit;limit.max_milliseconds=40;evrm::NativePlayerRoster roster;evrm::NativeOwnerCache cache;std::string why;require(cache.players(owned.memory,owned.identity,roster,why,limit),"owned roster capture failed");
 auto api=backend_fixture();auto& b=*api->impl;auto sources=native_sources(b);auto owner=std::make_shared<BackendOwnerSource>(owned);api->test_owner(owner);b.replacement=true;
 const auto queue=handle<VkQueue>(4);const auto command=handle<VkCommandBuffer>(0x700);
 auto first=std::make_shared<evrm::PreparedAvatar>();first->asset=std::make_shared<vrm::Avatar>();auto second=std::make_shared<evrm::PreparedAvatar>();second->asset=std::make_shared<vrm::Avatar>();
 auto local=selection({},11,first),remote=selection("fixture-remote",12,second);local.identity=roster.players.at(0);remote.identity=roster.players.at(1);api->retain_player_models({local,remote});
 for(auto* target:{&local,&remote}){require(api->prepare_player_model(queue,0,target->candidate)==evrm::NativeAvatarUploadState::Pending,"fixture model not pending");require(api->prepare_player_model(queue,0,target->candidate)==evrm::NativeAvatarUploadState::Ready&&api->activate_player_model(*target->candidate,accept,nullptr),"fixture model activation failed");target->selection=target->candidate;target->candidate.reset();api->retain_player_models({local,remote});}
 fixture_runtime_subjects=std::make_shared<const std::vector<evrm::AvatarSubjectSelection>>(std::vector<evrm::AvatarSubjectSelection>{local,remote});
 owned.memory.reset();const auto capture_started=std::chrono::steady_clock::now();
 auto poses=api->player_pose_owners(b.metadata.buffer_epoch(),{local,remote});require(poses.size()==2&&owner->captures==1,"backend repeated capture for each actor");std::cout<<"METRIC backend-targets=2 roster=3 capture-calls="<<owned.memory.calls<<" capture-bytes="<<owned.memory.bytes<<" capture-ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-capture_started).count()<<"\n";
 unsigned publication_serial=20;
 const auto record=[&](bool two=true){
  auto make=[&](unsigned n){auto x=std::make_shared<evrm::Bundle>(b.context);const auto& choice=n?remote:local;auto p=std::make_shared<evrm::NativeAvatarPublication>();p->subject=choice.subject;p->player_identity=choice.identity;p->assignment_generation=choice.selection->generation;p->asset=choice.model->asset;p->pose_owner=poses[n].owner;p->parts={{n+1,42,0,0}};p->instance_qualifiers=p->parts;p->serial=publication_serial;p->presentation_serial=publication_serial;p->published_milliseconds=GetTickCount64();p->source_instances=0x12340000;p->source_instance_range=8192;p->source_buffer_epoch=b.metadata.buffer_epoch();x->pose=p;x->qualified_parts=p->parts;x->assets=b.player_models.at(choice.subject).assets;x->instances=sources.instances;x->arena=sources.arena;x->gbuf_camera=sources.arena;x->owner=sources.arena;x->sources={sources.instances,sources.arena};x->cpu_owner=poses[n].owner->candidate;x->prepared_owner_hint=std::shared_ptr<const evrm::NativeOwnerCandidate>(poses[n].owner,&poses[n].owner->candidate);x->recorded=x->ended=true;x->visibility_passes=x->gbuffer_passes=x->visibility_draws=x->gbuffer_draws=1;x->visibility_clips=x->gbuffer_clips=1;x->data.permit[3]=1;x->clip_set=handle<VkDescriptorSet>(0x800+n);x->first_pass.extent={1920,1080};x->control=evrm::make_buffer(b.context,sizeof(evrm::NativePrepareControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);x->frame=evrm::make_buffer(b.context,sizeof(evrm::NativePreparedFrame),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);require(evrm::make_native_body_mask(x->cpu_owner,p->parts,x->instances,x->body_mask,why),"fixture body mask failed");x->body_bits=evrm::make_buffer(b.context,sizeof(x->body_mask.words),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);return x;};
  auto a=make(0),z=make(1);auto publication=std::make_shared<evrm::NativeAvatarPublication>(*a->pose);if(two)publication->additional_subjects={z->pose};a->pose=publication;api->publish(queue,0,publication,b.metadata.buffer_epoch());
  auto& r=b.commands[command];r.eligible=true;r.affected_pass=true;r.ownership_closed=true;r.ownership_window=publication_serial;r.ownership_presentation=publication_serial;r.ownership_sources={sources.instances};require(b.prepare_player_group(r,two?std::vector<std::shared_ptr<evrm::Bundle>>{a,z}:std::vector<std::shared_ptr<evrm::Bundle>>{a}),"fixture group preparation failed");require(*static_cast<unsigned*>(a->body_bits->mapped)==(two?6u:2u),"unassigned third actor entered suppression union");++publication_serial;return a;
 };
 const auto submit_group=[&](bool expected){const auto root=b.commands.at(command).bundle;VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&command;require(api->before_submit(queue,1,&info)==VK_SUCCESS,"native submission unexpectedly rejected");require(bool(root->data.permit[0])==expected&&bool(root->subjects[0]->data.permit[0])==expected&&bool(*static_cast<unsigned*>(root->common_gate->mapped))==expected,"CPU subject permits and common clipping disagree");
  require(root->data.config[3]==0.f&&root->subjects[0]->data.config[3]==(expected?1.f:0.f),"remote anchor role escaped final cohort/account admission or changed local mode");
  evrm::NativePrepareControl uploaded{};std::memcpy(&uploaded,root->subjects[0]->control->mapped,sizeof uploaded);require(uploaded.config[3]==root->subjects[0]->data.config[3],"remote role was not written to the actual GPU control");api->submission_result(queue,VK_SUCCESS);api->command_reset(command);};
 record();const auto verify_before=owner->verifies;owned.memory.reset();const auto verify_started=std::chrono::steady_clock::now();submit_group(true);std::cout<<"METRIC backend-targets=2 verify-calls="<<owned.memory.calls<<" verify-bytes="<<owned.memory.bytes<<" submit-host-ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-verify_started).count()<<"\n";require(owner->verifies==verify_before+1&&owner->verified_subjects==2&&owned.memory.prefix_reads==1,"submit did not validate cohort once");
 // Changing an unrelated census record allows only a complete fresh cohort,
 // with the exact original actor selection and original pose age preserved.
 record();const auto before_refresh=owner->captures;owned.entities[2*64+20]^=1;submit_group(true);require(owner->captures==before_refresh+1,"census scenario never exercised refresh");owned.entities[2*64+20]^=1;
 record()->subjects[0]->qualified_parts[0][1]^=1;submit_group(false);
 record()->subjects[0]->invalid=true;submit_group(false);
 record();owned.entities[64+20]^=1;submit_group(false);owned.entities[64+20]^=1;
 record();owned.memory.reset();owned.memory.advance_after=3;owned.memory.advanced=140;submit_group(false);owned.memory.advance_after=std::numeric_limits<std::size_t>::max();owned.memory.reset();
 record();auto reassigned=remote;reassigned.selection=std::make_shared<evrm::AvatarCandidate>(evrm::AvatarCandidate{99,second,remote.subject});fixture_runtime_subjects=std::make_shared<const std::vector<evrm::AvatarSubjectSelection>>(std::vector<evrm::AvatarSubjectSelection>{local,reassigned});submit_group(false);fixture_runtime_subjects=std::make_shared<const std::vector<evrm::AvatarSubjectSelection>>(std::vector<evrm::AvatarSubjectSelection>{local,remote});
 record();owned.infos[168]^=1;submit_group(false);owned.infos[168]^=1;
 record();remote.replacement_enabled=false;fixture_runtime_subjects=std::make_shared<const std::vector<evrm::AvatarSubjectSelection>>(std::vector<evrm::AvatarSubjectSelection>{local,remote});submit_group(false);remote.replacement_enabled=true;fixture_runtime_subjects=std::make_shared<const std::vector<evrm::AvatarSubjectSelection>>(std::vector<evrm::AvatarSubjectSelection>{local,remote});
 {auto preview=record(false);preview->menu_preview=true;preview->menu_owner.kind=evrm::NativeMenuOwnerKind::frontend;auto pose=std::make_shared<evrm::NativeAvatarPublication>(*preview->pose);pose->pose_owner.reset();preview->pose=pose;b.view_publications=pose;const auto before=owner->menu_verifies;
 VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&command;require(api->before_submit(queue,1,&info)==VK_SUCCESS&&owner->menu_verifies==before+1,"account-bound local preview bypassed legacy menu authorization");api->submission_result(queue,VK_SUCCESS);api->command_reset(command);}

 // An unrequested remote absence is retained in the one included local proof.
 // Spawn can refresh to a complete safe bank; overlapping unassigned ownership
 // must revoke the exact same local pose and its native-body suppression.
 const auto submit_single=[&](bool expected){const auto root=b.commands.at(command).bundle;VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&command;require(root->subjects.empty()&&*static_cast<unsigned*>(root->body_bits->mapped)==2,"unavailable third body entered local suppression union");require(api->before_submit(queue,1,&info)==VK_SUCCESS&&bool(root->data.permit[0])==expected,"partial local submit lost negative/all-active refresh proof");api->submission_result(queue,VK_SUCCESS);api->command_reset(command);};
 for(bool overlap_spawn:{false,true}){
  backend_absent_actor(owned,3,true);b.player_observed_at=GetTickCount64()-251;owned.memory.reset();poses=api->player_pose_owners(b.metadata.buffer_epoch(),{local,remote});require(poses.size()==2,"unassigned absent remote removed ready cohort");
  auto alone=record(false);const auto absent_refresh=owner->captures;backend_absent_actor(owned,3,false);
  if(overlap_spawn)put<std::uint32_t>(owned.player_storage,3200,0x01000001);
  submit_single(!overlap_spawn);require(owner->captures==absent_refresh+1,"negative occupancy mutation did not force fresh all-active capture");
  require(alone->pose->pose_owner==poses[0].owner,"refresh renewed pose lifetime/selection");
  if(overlap_spawn)put<std::uint32_t>(owned.player_storage,3200,0x01000003);
 }
 poses=api->player_pose_owners(b.metadata.buffer_epoch(),{local,remote});require(poses.size()==2,"valid cohort not restored");
 record();b.metadata.buffer_destroyed(sources.instances.info.buffer);submit_group(false);
 // Re-enable uses a fresh device adoption before new work; stale records were
 // already rejected above while their resource leases stayed alive.
 std::cout<<"PASS actual before_submit: two models, batch owned-memory verification, census refresh, account change and None revocation\n";
}

void owner_observation_cases(){
 // Real production capture traverses a synthetic original-engine roster. An
 // unrelated partially initialized third slot must still fail closed, while
 // diagnostics explain why even the requested local owner was unavailable.
 BackendPlayersFixture owned(3);owned.memory.batched=true;
 evrm::NativePlayerRoster roster;evrm::NativeOwnerCache cache;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;std::string why;
 require(cache.players(owned.memory,owned.identity,roster,why,limits),"owner observation fixture roster unavailable");
 auto api=backend_fixture();auto& b=*api->impl;native_sources(b);b.replacement=true;
 auto source=std::make_shared<BackendOwnerSource>(owned);api->test_owner(source);
 auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=std::make_shared<vrm::Avatar>();
 auto local=selection({},71,model);local.identity=roster.players.at(0);api->retain_player_models({local});
 evrm::log_event("fixture.owner-observation-begin");
 put<std::uint32_t>(owned.infos,2*160,0);
 for(unsigned i=0;i<3;++i){owned.memory.reset();require(api->player_pose_owners(b.metadata.buffer_epoch(),{local}).empty(),"unsettled unrelated roster entry bypassed existing ownership refusal");}
 put<std::uint32_t>(owned.infos,2*160,3);owned.memory.reset();
 const auto recovered=api->player_pose_owners(b.metadata.buffer_epoch(),{local});
 require(recovered.size()==1&&recovered[0].subject.empty()&&recovered[0].owner->candidate.account==local.identity.account,"observation changed recovered local identity");
 require(api->player_pose_owners(b.metadata.buffer_epoch(),{local})[0].owner==recovered[0].owner,"diagnostic changed warm owner reuse");
 evrm::log_event("fixture.owner-observation-end");
 std::cout<<"PASS actual owner capture failure, bounded observation and same-identity recovery\n";
}


std::size_t backend_actor_slot(const Fixture& f,unsigned actor){for(std::size_t i=0;i<Fixture::capacity;++i){std::uint32_t id;std::memcpy(&id,f.keys.data()+i*4,4);if(id==actor)return i;}throw std::runtime_error("actor missing");}
void backend_absent_actor(Fixture& f,unsigned actor,bool absent){auto slot=backend_actor_slot(f,actor);if(absent)f.occupancy[slot/8]&=std::uint8_t(~(1u<<(slot%8)));else f.occupancy[slot/8]|=1u<<(slot%8);}
void lifecycle_owner_cases(){
 BackendPlayersFixture owned(3);owned.memory.batched=true;evrm::NativeOwnerCache cache;evrm::NativePlayerRoster roster;std::string why;
 require(cache.players(owned.memory,owned.identity,roster,why),"lifecycle roster unavailable");
 auto api=backend_fixture();auto& b=*api->impl;native_sources(b);b.replacement=true;auto source=std::make_shared<BackendOwnerSource>(owned);api->test_owner(source);
 auto model=std::make_shared<evrm::PreparedAvatar>();model->asset=std::make_shared<vrm::Avatar>();
 auto local=selection({},80,model),remote=selection("fixture-remote",81,model),other=selection("fixture-other",82,model);local.identity=roster.players[0];remote.identity=roster.players[1];other.identity=roster.players[2];
 backend_absent_actor(owned,2,true);api->retain_player_models({remote,local,other});
 const auto epoch=b.metadata.buffer_epoch();auto values=api->player_pose_owners(epoch,{remote,local,other});
 require(values.size()==2&&values[0].subject.empty()&&values[0].owner->candidate.entity_id==1&&values[1].subject==other.subject&&values[1].owner->candidate.entity_id==3,"partial availability lost ready subjects or row mapping");
 const auto captures=source->captures;for(unsigned i=0;i<4;++i){auto cached=api->player_pose_owners(epoch,{remote,local,other});require(cached.size()==2&&cached[0].owner==values[0].owner,"partial proposal not reused");}
 require(source->captures==captures,"unavailable request forced capture every presentation");
 backend_absent_actor(owned,2,false);b.player_observed_at=GetTickCount64()-251;values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==3&&values[0].subject==remote.subject&&values[0].owner->candidate.entity_id==2&&source->captures==captures+1,"spawn did not refresh within normal cadence");
 remote.replacement_enabled=false;api->retain_player_models({remote,local,other});values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==2&&values[0].subject.empty(),"None retained old row or dropped local");
 local.replacement_enabled=false;remote.replacement_enabled=true;api->retain_player_models({remote,local,other});values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==2&&values[0].subject==remote.subject&&values[0].owner->candidate.entity_id==2,"local None misdirected remote-only mapping");
 remote.identity.connection+=64;api->retain_player_models({remote,local,other});values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==1&&values[0].subject==other.subject,"changed connection reused stale association");
 remote.identity=roster.players[1];remote.identity.local=true;api->retain_player_models({remote,local,other});values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==1&&values[0].subject==other.subject,"changed local role reused stale association");
 remote.identity=roster.players[1];api->retain_player_models({remote,local,other});values=api->player_pose_owners(epoch,{remote,local,other});require(values.size()==2,"reconnect did not restore correct subject");
 const auto before_epoch=source->captures;VkBufferCreateInfo fresh{};fresh.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;fresh.size=64;fresh.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;b.metadata.buffer_created(fake_player_gpu::handle<VkBuffer>(0x778899),fresh);const auto next_epoch=b.metadata.buffer_epoch();require(next_epoch!=epoch,"fixture epoch unchanged");values=api->player_pose_owners(next_epoch,{remote,local,other});require(values.size()==2&&source->captures==before_epoch+1,"epoch change silently renewed proposal");

 // A fully unavailable requested set still observes the cadence and keys.
 other.replacement_enabled=false;backend_absent_actor(owned,2,true);api->retain_player_models({remote,local,other});
 require(api->player_pose_owners(next_epoch,{remote,local,other}).empty(),"absent-only subject unexpectedly acquired an owner");const auto zero_captures=source->captures;
 require(api->player_pose_owners(next_epoch,{remote,local,other}).empty()&&source->captures==zero_captures,"zero-ready partial batch bypassed cooldown");
 auto replaced_source=std::make_shared<BackendOwnerSource>(owned);api->test_owner(replaced_source);require(api->player_pose_owners(next_epoch,{remote,local,other}).empty()&&replaced_source->captures==1,"zero-ready cache survived owner source replacement");
 backend_absent_actor(owned,2,false);b.player_observed_at=GetTickCount64()-251;require(api->player_pose_owners(next_epoch,{remote,local,other}).size()==1,"remote did not return ready");
 const auto before_none=replaced_source->captures;remote.replacement_enabled=false;api->retain_player_models({remote,local,other});require(api->player_pose_owners(next_epoch,{remote,local,other}).empty(),"None kept a proposal");remote.replacement_enabled=true;api->retain_player_models({remote,local,other});require(api->player_pose_owners(next_epoch,{remote,local,other}).size()==1&&replaced_source->captures==before_none+1,"all-None request failed to invalidate prior proposal");
 b.metadata.buffer_destroyed(fake_player_gpu::handle<VkBuffer>(0x12340000));require(api->player_pose_owners(b.metadata.buffer_epoch(),{remote,local,other}).empty(),"destroyed positive source reused cached availability");
 std::cout<<"PASS lifecycle partial row mapping, cooldown, spawn, None, connection/role and epoch/source invalidation\n";
}
void uncertain_cases(){
 using namespace fake_player_gpu;auto api=backend_fixture();auto& b=*api->impl;const auto queue=handle<VkQueue>(4);const auto command=handle<VkCommandBuffer>(0x900);b.queue=queue;
 auto make=[&](unsigned bit){auto out=std::make_shared<evrm::Bundle>(b.context);out->frame=evrm::make_buffer(b.context,sizeof(evrm::NativePreparedFrame),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);out->control=evrm::make_buffer(b.context,sizeof(evrm::NativePrepareControl),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);out->body_bits=evrm::make_buffer(b.context,sizeof(out->body_mask.words),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);out->body_mask.words[0]=bit;out->data.permit[0]=1;out->submitted=out->awaiting_submit_result=true;out->submitted_queue=queue;return out;};
 auto root=make(2),child=make(4);auto& r=b.commands[command];require(b.prepare_player_group(r,{root,child}),"uncertain fixture group failed");b.common_cpu_permit(root,true);const auto allocated=memory.size();const auto released=destroys;idle_result=VK_ERROR_UNKNOWN;
 api->submission_result(queue,VK_ERROR_UNKNOWN);require(b.context.uncertain&&root->failed_submit&&child->failed_submit&&destroys==released&&memory.size()==allocated,"uncertain submit released group resources");
 VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;info.commandBufferCount=1;info.pCommandBuffers=&command;
 require(api->before_submit(queue,1,&info)==VK_ERROR_UNKNOWN&&root->data.permit[0]&&child->data.permit[0]&&*static_cast<unsigned*>(root->common_gate->mapped)==1,"uncertain retry edited potentially pending permits");
 idle_result=VK_SUCCESS;require(api->before_submit(queue,1,&info)==VK_SUCCESS&&!root->data.permit[0]&&!child->data.permit[0]&&!(*static_cast<unsigned*>(root->common_gate->mapped)),"proven completion did not revoke all retry controls");
 root.reset();child.reset();api.reset();require(memory.size()<allocated,"device-idle teardown did not retire group resources");
 std::cout<<"PASS actual uncertain submit: group leases, immutable pending controls, proven-idle retry and teardown\n";
}

void shared_volume_cases(){
 using namespace fake_player_gpu;auto api=backend_fixture();auto& b=*api->impl;
 evrm::NativeVolumeSnapshotCapabilities caps;caps.memory.memoryTypeCount=caps.memory.memoryHeapCount=1;caps.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;caps.memory.memoryHeaps[0].size=64*1024*1024;
 auto& l=caps.limits;l.maxImageDimension3D=1024;l.maxBoundDescriptorSets=8;l.maxPerStageDescriptorSampledImages=l.maxDescriptorSetSampledImages=l.maxPerStageDescriptorStorageImages=l.maxDescriptorSetStorageImages=8;l.maxPerStageResources=16;l.maxComputeWorkGroupInvocations=64;for(unsigned n=0;n<3;++n){l.maxComputeWorkGroupSize[n]=64;l.maxComputeWorkGroupCount[n]=1024;}
 for(auto& format:caps.formats)format=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
 for(auto& out:caps.outputs){out.maxExtent={1024,1024,128};out.maxMipLevels=out.maxArrayLayers=1;out.sampleCounts=VK_SAMPLE_COUNT_1_BIT;out.maxResourceSize=64*1024*1024;}
 std::array<evrm::NativeVolumeSnapshotSource,2> sources;for(unsigned n=0;n<2;++n){auto& v=sources[n];v.image=handle<VkImage>(100+n);v.generation=1;v.extent={4,4,4};v.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;v.format=n?VK_FORMAT_R16_UNORM:VK_FORMAT_R32_UINT;}
 sources[0].flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;sources[0].format_count=2;sources[0].formats[0]=VK_FORMAT_R32_UINT;sources[0].formats[1]=VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
 const std::array<unsigned,5> words{0x07230203,0x10000,0,1,0};const auto before=images_created;
 auto a=std::make_shared<evrm::Bundle>(b.context),z=std::make_shared<evrm::Bundle>(b.context);a->volume_snapshot=std::make_shared<evrm::NativeVolumeSnapshot>(b.context.device,get,sources,caps,words.data(),words.size());a->snapshot_attempted=a->snapshot_recorded=true;a->snapshot_extent={4,4,4};a->snapshot_dispatch={1,1,1};a->volume_storage[0].view.image=handle<VkImage>(100);a->volume_allocations[0].image=handle<VkImage>(100);
 b.share_player_volume(*z,*a);require(z->volume_snapshot==a->volume_snapshot&&images_created-before==2,"two BLEND actors allocated duplicate native-volume snapshots");require(z->snapshot_recorded&&z->snapshot_attempted&&z->snapshot_extent.width==4&&z->volume_storage[0].view.image==handle<VkImage>(100)&&z->volume_allocations[0].image==handle<VkImage>(100),"shared volume dropped source provenance");
 const auto outputs=z->volume_snapshot->outputs();const auto destroyed=images_destroyed;a.reset();require(z->volume_snapshot->outputs()[0].imageView==outputs[0].imageView&&images_destroyed==destroyed,"first subject retirement destroyed shared snapshot");z.reset();require(images_destroyed==destroyed+2,"last subject retirement leaked volume resources");
 std::cout<<"PASS actual shared volume resources: two images per view, retained provenance and last-subject retirement\n";
}
void preparation_diagnostic_cases(){
 using namespace fake_player_gpu;
 evrm::log_event("fixture.completion-preparation-begin");
 for(bool pending_child:{false,true}){
  auto api=backend_fixture();auto& backend=*api->impl;evrm::Recording r;
  auto make=[&](bool local){auto b=std::make_shared<evrm::Bundle>(backend.context);
   b->frame=evrm::make_buffer(backend.context,local?sizeof(evrm::NativePreparedFrame):evrm::native_oit_prepared_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);b->submitted=true;b->submitted_queue=handle<VkQueue>(4);b->data.permit[0]=1;
   auto pose=std::make_shared<evrm::NativeAvatarPublication>();pose->serial=local?1:2;pose->subject=local?"":"PRIVATE-DIAGNOSTIC-TOKEN";
   pose->player_identity={0x1020304050607080ULL,local?1u:2u,local?64u:129u,local};b->pose=pose;return b;};
  auto root=make(true),remote=make(false);r.bundle=root;root->subjects={remote};remote->material_plan.mixed=true;
  static_cast<evrm::NativePreparedFrame*>(root->frame->mapped)->rootScale[3]=0x3f800000u;
  static_cast<evrm::NativePreparedFrame*>(root->frame->mapped)->screen[0]=.5f;
  static_cast<evrm::NativePreparedFrame*>(root->frame->mapped)->screen[1]=.5f;
  evrm::NativeOitPrepareDiagnostic diagnostic;diagnostic.magic=evrm::native_oit_diagnostic_magic;diagnostic.checkpoint=4;diagnostic.valid_mask=2;diagnostic.w[1]=0x3f800000u;
  std::memcpy(static_cast<unsigned char*>(remote->frame->mapped)+evrm::native_oit_diagnostic_offset,&diagnostic,sizeof diagnostic);
  remote->awaiting_submit_result=pending_child;
  const auto snapshot=[](const auto& b){const auto* data=static_cast<const unsigned char*>(b->frame->mapped);return std::vector<unsigned char>(data,data+b->frame->size);};
  const auto before_root=snapshot(root),before_remote=snapshot(remote);
  backend.harvest_history(r,"owned-fixture-completion");
  require(snapshot(root)==before_root&&snapshot(remote)==before_remote,"completion diagnostic changed GPU frame bytes or admission");
  require(backend.history_completed==1,"completed group did not retire once");
  backend.harvest_history(r,"owned-fixture-completion");require(backend.history_completed==1,"same completed group was harvested twice");
 }
 std::cout<<"PASS completed group diagnostics preserve frame bytes and skip pending children\n";
}
int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 std::set_terminate([]{std::fputs("FAIL terminate\n",stderr);std::_Exit(98);});
 std::signal(SIGABRT,[](int){std::fputs("FAIL abort\n",stderr);std::_Exit(99);});
 SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{std::fprintf(stderr,"FAIL unhandled exception=%08lx\n",e&&e->ExceptionRecord?e->ExceptionRecord->ExceptionCode:0UL);std::fflush(stderr);std::_Exit(97);});
 try{
  require(argc==2,"shader directory argument required");fixture_shaders=std::filesystem::path(argv[1]);
  require(has_players<evrm::NativeBackend>::value,"backend cannot coordinate per-player owners and model activation");
  require(has_group<evrm::NativeBackend::Impl>::value,"backend has no shared subject preparation/commit path");
  group_cases<evrm::NativeBackend::Impl>();staged_ready_cases();asset_cases();iteration_history_cases();submission_cases();uncertain_cases();shared_volume_cases();preparation_diagnostic_cases();owner_observation_cases();lifecycle_owner_cases();
  std::cout<<"PASS per-player backend coordinator\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
