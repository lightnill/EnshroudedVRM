#define VK_NO_PROTOTYPES
#include "../src/native_oit_draw.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <windows.h>

namespace {
template<class T>T h(std::uintptr_t n) noexcept {if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(n);else return static_cast<T>(n);}
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
const char* scenario="startup";
struct Pipeline {VkPipeline handle{};VkPipelineLayout layout{};VkShaderModule vertex{},fragment{};VkFrontFace winding{};unsigned colors{};bool live=false;char entry[32]{};};
struct Event {char kind{};VkPipeline pipeline{};VkPipelineLayout layout{};unsigned first=0,count=0;std::array<VkDescriptorSet,32> sets{};};
std::array<Pipeline,64> pipelines{};
std::array<Event,1024> events{};
unsigned creates=0,destroys=0,event_count=0;
unsigned fail_create_at=0;
bool callbacks_ok=true,fail_create=false,null_success=false;
Event* event(char kind) noexcept {if(event_count>=events.size()){callbacks_ok=false;return nullptr;}auto* e=&events[event_count++];*e={};e->kind=kind;return e;}
VkResult VKAPI_CALL create(VkDevice,VkPipelineCache,unsigned count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out) noexcept {
    if(count!=1||!info||!out||creates>=pipelines.size()||info->stageCount!=2||!info->pStages||!info->pRasterizationState||!info->pColorBlendState){callbacks_ok=false;return VK_ERROR_UNKNOWN;}
    auto& p=pipelines[creates++];p.handle=h<VkPipeline>(100+creates);p.live=true;p.layout=info->layout;
    p.vertex=info->pStages[0].module;p.fragment=info->pStages[1].module;p.winding=info->pRasterizationState->frontFace;p.colors=info->pColorBlendState->attachmentCount;
    std::snprintf(p.entry,sizeof(p.entry),"%s",info->pStages[1].pName);
    if(std::strcmp(info->pStages[0].pName,p.colors==0?"vs_shadow":"vs_native"))callbacks_ok=false;
    const auto* depth=info->pDepthStencilState;
    if(!depth||!depth->depthTestEnable||depth->depthWriteEnable!=(p.colors<=2?VK_TRUE:VK_FALSE)||depth->depthCompareOp!=(p.colors==6?VK_COMPARE_OP_EQUAL:VK_COMPARE_OP_GREATER))callbacks_ok=false;
    if(p.colors==6){for(unsigned i=0;i<6;++i)if(info->pColorBlendState->pAttachments[i].colorWriteMask!=15u)callbacks_ok=false;}
    if(p.colors==2&&info->pColorBlendState->pAttachments[1].colorWriteMask)callbacks_ok=false;
    if(p.colors==5){const auto* a=info->pColorBlendState->pAttachments;if(!a[0].blendEnable||!a[4].blendEnable||a[0].dstColorBlendFactor!=VK_BLEND_FACTOR_ONE||a[4].dstColorBlendFactor!=VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR)callbacks_ok=false;}
    if(null_success){p.live=false;*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    *out=p.handle;return fail_create||creates==fail_create_at?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
void VKAPI_CALL destroy(VkDevice,VkPipeline pipeline,const VkAllocationCallbacks*) noexcept {
    for(auto& p:pipelines)if(p.handle==pipeline&&p.live){p.live=false;++destroys;return;}
    callbacks_ok=false;
}
void VKAPI_CALL bind(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline pipeline) noexcept {
    if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS)callbacks_ok=false;
    if(auto* e=event('p'))e->pipeline=pipeline;
}
void VKAPI_CALL descriptors(VkCommandBuffer,VkPipelineBindPoint point,VkPipelineLayout layout,unsigned first,unsigned count,const VkDescriptorSet* sets,unsigned dynamic,const unsigned*) noexcept {
    if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS||dynamic||!sets||count>32){callbacks_ok=false;return;}
    if(auto* e=event('s')){e->layout=layout;e->first=first;e->count=count;for(unsigned i=0;i<count;++i)e->sets[i]=sets[i];}
}
void VKAPI_CALL draw(VkCommandBuffer,unsigned count,unsigned instances,unsigned first,unsigned first_instance) noexcept {
    if(instances!=1||first_instance)callbacks_ok=false;
    if(auto* e=event('d')){e->first=first;e->count=count;}
}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name) noexcept {
    if(!std::strcmp(name,"vkCreateGraphicsPipelines"))return reinterpret_cast<PFN_vkVoidFunction>(create);
    if(!std::strcmp(name,"vkDestroyPipeline"))return reinterpret_cast<PFN_vkVoidFunction>(destroy);
    if(!std::strcmp(name,"vkCmdBindPipeline"))return reinterpret_cast<PFN_vkVoidFunction>(bind);
    if(!std::strcmp(name,"vkCmdBindDescriptorSets"))return reinterpret_cast<PFN_vkVoidFunction>(descriptors);
    if(!std::strcmp(name,"vkCmdDraw"))return reinterpret_cast<PFN_vkVoidFunction>(draw);
    return nullptr;
}
const auto device=h<VkDevice>(1);const auto basic=h<VkPipelineLayout>(2);const auto surface=h<VkPipelineLayout>(3);
const auto vs=h<VkShaderModule>(4);const auto ps=h<VkShaderModule>(5);const auto sv=h<VkShaderModule>(6);const auto sf=h<VkShaderModule>(7);
const auto command=h<VkCommandBuffer>(8);const auto native=h<VkPipeline>(9);const auto compatible=h<VkPipelineLayout>(10);
const std::array<VkDescriptorSet,3> native_sets={h<VkDescriptorSet>(20),h<VkDescriptorSet>(21),h<VkDescriptorSet>(22)};
std::array<evrm::NativeDrawCall,3> calls={evrm::NativeDrawCall{h<VkDescriptorSet>(30),0,3},evrm::NativeDrawCall{h<VkDescriptorSet>(31),3,6,h<VkDescriptorSet>(40)},evrm::NativeDrawCall{h<VkDescriptorSet>(32),9,3}};
void clear(){event_count=0;}
void restored(){need(event_count>=2,"native restoration absent");const auto& p=events[event_count-2];const auto& s=events[event_count-1];need(p.kind=='p'&&p.pipeline==native,"native pipeline restoration");need(s.kind=='s'&&s.layout==compatible&&s.first==0&&s.count==native_sets.size()&&s.sets[0]==native_sets[0]&&s.sets[1]==native_sets[1]&&s.sets[2]==native_sets[2],"complete native descriptor restoration");}
void routing(VkPipeline bp,VkPipeline sp,bool enhanced,bool shadow=false){
    restored();unsigned drawn=0;VkPipeline bound{};VkPipelineLayout layout{};VkDescriptorSet set{};
    for(unsigned i=0;i<event_count-2;++i){const auto& e=events[i];if(e.kind=='p')bound=e.pipeline;if(e.kind=='s'&&e.first==0){layout=e.layout;set=e.sets[0];need(e.count==1,"material set count");}
        if(e.kind=='d'){need(drawn<calls.size(),"extra original draw");const auto& c=calls[drawn++];const bool use=enhanced&&bool(c.surface_set);need(bound==(use?sp:bp)&&layout==(use?surface:basic)&&set==(use?c.surface_set:c.set),"per-material pipeline/layout/set routing");need(e.first==c.first_index&&e.count==c.index_count,"original draw range changed");}}
    need(drawn==calls.size(),"original draw omitted");if(shadow)need(events[1].kind=='s'&&events[1].first==1&&events[1].sets[0]==h<VkDescriptorSet>(50),"shadow view set missing");
}
template<class Record>void invalid(Record record){
    const auto saved=calls;
    const auto reject=[&](const char* why){clear();need(!record(command,calls.data(),3,native,compatible,3,native_sets.data())&&event_count==0,why);};
    calls[2].set={};reject("null late Basic set recorded partial commands");calls=saved;
    calls[1].set={};reject("null Basic set with enhanced descriptor accepted");calls=saved;
    calls[2].index_count=0;reject("empty late draw recorded partial commands");calls=saved;
    calls[2].index_count=4;reject("nontriangle late draw accepted");calls=saved;
    calls[2].first_index=std::numeric_limits<unsigned>::max()-1;reject("overflowing late draw accepted");calls=saved;
    const auto rejected=[&](VkCommandBuffer c,const evrm::NativeDrawCall* in,unsigned count,VkPipeline p,VkPipelineLayout l,unsigned n,const VkDescriptorSet* sets){clear();need(!record(c,in,count,p,l,n,sets)&&event_count==0,"invalid outer input recorded commands");};
    rejected({},calls.data(),3,native,compatible,3,native_sets.data());rejected(command,nullptr,3,native,compatible,3,native_sets.data());
    rejected(command,calls.data(),0,native,compatible,3,native_sets.data());rejected(command,calls.data(),4097,native,compatible,3,native_sets.data());
    rejected(command,calls.data(),3,{},compatible,3,native_sets.data());rejected(command,calls.data(),3,native,{},3,native_sets.data());
    rejected(command,calls.data(),3,native,compatible,0,native_sets.data());rejected(command,calls.data(),3,native,compatible,33,native_sets.data());
    rejected(command,calls.data(),3,native,compatible,3,nullptr);auto sets=native_sets;sets[2]={};rejected(command,calls.data(),3,native,compatible,3,sets.data());
}
template<class P>void installs(P& p){
    need(!p.surface_ready(),"variant installed eagerly");const auto before=creates;
    need(!p.install_surface_variant({},sv,sf)&&!p.install_surface_variant(surface,{},sf)&&!p.install_surface_variant(surface,sv,{}),"invalid variant install accepted");need(creates==before,"invalid variant install allocated");
    fail_create=true;need(!p.install_surface_variant(surface,sv,sf)&&!p.surface_ready(),"failed variant became ready");fail_create=false;
    need(destroys>0&&!pipelines[creates-1].live,"failed nonnull pipeline leaked");
    null_success=true;need(!p.install_surface_variant(surface,sv,sf)&&!p.surface_ready(),"null successful pipeline became ready");null_success=false;
}
void gbuffer(){scenario="gbuffer";const unsigned first=creates;
    evrm::NativeDrawPipelines p(device,get,basic,vs,ps,ps);
    const auto record=[&](VkCommandBuffer c,const evrm::NativeDrawCall* in,unsigned n,VkPipeline original,VkPipelineLayout l,unsigned sets,const VkDescriptorSet* s){return p.record(c,true,in,n,original,l,sets,s);};
    need(creates==first+2,"Basic constructor created enhanced pipeline");clear();need(!record(command,calls.data(),3,native,compatible,3,native_sets.data())&&event_count==0,"unavailable enhanced GBuffer recorded partial commands");
    clear();need(p.record(command,false,calls.data(),3,native,compatible,3,native_sets.data()),"Basic visibility rejected optional set");routing(pipelines[first].handle,{},false);
    installs(p);auto only=calls;only[1].surface_set={};clear();need(record(command,only.data(),3,native,compatible,3,native_sets.data()),"failed enhance poisoned Basic GBuffer");
    need(p.install_surface_variant(surface,sv,sf)&&p.surface_ready(),"enhanced GBuffer install failed");const auto enhanced=pipelines[creates-1];need(enhanced.layout==surface&&enhanced.vertex==sv&&enhanced.fragment==sf&&!std::strcmp(enhanced.entry,"ps_gbuffer")&&enhanced.winding==VK_FRONT_FACE_CLOCKWISE&&enhanced.colors==6,"enhanced GBuffer construction");
    const auto installed=creates;need(!p.install_surface_variant(surface,sv,sf)&&creates==installed,"enhancement overwritten");
    clear();need(record(command,calls.data(),3,native,compatible,3,native_sets.data()),"mixed GBuffer rejected");routing(pipelines[first+1].handle,enhanced.handle,true);invalid(record);
    clear();need(p.record(command,false,calls.data(),3,native,compatible,3,native_sets.data()),"visibility failed with ready variant");routing(pipelines[first].handle,{},false);
    std::puts("PASS GBuffer: lazy install, mixed routing, visibility Basic, preflight, native restoration");
}
void oit(){scenario="oit";const unsigned first=creates;evrm::NativeOitDrawPipelines p(device,get,basic,vs,ps,true);
    const auto record=[&](VkCommandBuffer c,const evrm::NativeDrawCall* in,unsigned n,VkPipeline original,VkPipelineLayout l,unsigned sets,const VkDescriptorSet* s){return p.record(c,in,n,original,l,sets,s);};
    need(creates==first+1&&pipelines[first].winding==VK_FRONT_FACE_COUNTER_CLOCKWISE,"Basic OIT winding/eagerness changed");
    clear();need(!record(command,calls.data(),3,native,compatible,3,native_sets.data())&&event_count==0,"unavailable enhanced OIT recorded partial commands");
    installs(p);auto only=calls;only[1].surface_set={};clear();need(record(command,only.data(),3,native,compatible,3,native_sets.data()),"failed enhance poisoned Basic OIT");
    need(p.install_surface_variant(surface,sv,sf)&&p.surface_ready(),"enhanced OIT install failed");const auto enhanced=pipelines[creates-1];need(enhanced.layout==surface&&enhanced.vertex==sv&&enhanced.fragment==sf&&!std::strcmp(enhanced.entry,"ps_oit")&&enhanced.winding==VK_FRONT_FACE_CLOCKWISE&&enhanced.colors==5,"enhanced OIT construction");
    const auto installed=creates;need(!p.install_surface_variant(surface,sv,sf)&&creates==installed,"OIT enhancement overwritten");
    clear();need(record(command,calls.data(),3,native,compatible,3,native_sets.data()),"mixed OIT rejected");routing(pipelines[first].handle,enhanced.handle,true);invalid(record);
    std::puts("PASS OIT: mixed routing, distinct winding, failed creation isolation, preflight, native restoration");
}
void shadow(){scenario="shadow";const unsigned first=creates;evrm::NativeShadowDrawPipeline p(device,get,basic,vs,ps);
    const auto record=[&](VkCommandBuffer c,const evrm::NativeDrawCall* in,unsigned n,VkPipeline original,VkPipelineLayout l,unsigned sets,const VkDescriptorSet* s){return p.record(c,h<VkDescriptorSet>(50),in,n,original,l,sets,s);};
    clear();need(record(command,calls.data(),3,native,compatible,3,native_sets.data()),"shadow rejected optional set");routing(pipelines[first].handle,{},false,true);invalid(record);
    clear();need(!p.record(command,{},calls.data(),3,native,compatible,3,native_sets.data())&&event_count==0,"null shadow view accepted");std::puts("PASS shadow: optional surface ignored, original draws, view set, native restoration");
}
void constructor_failures(){
    scenario="constructor failures";const auto clean=[](){for(const auto& p:pipelines)need(!p.live,"failed Basic constructor leaked a pipeline");};
    fail_create_at=creates+2;bool threw=false;
    try{evrm::NativeDrawPipelines p(device,get,basic,vs,ps,ps);}catch(const std::runtime_error&){threw=true;}
    need(threw,"failed second Basic pipeline accepted");clean();fail_create_at=0;
    fail_create=true;threw=false;
    try{evrm::NativeOitDrawPipelines p(device,get,basic,vs,ps,true);}catch(const std::runtime_error&){threw=true;}
    need(threw,"failed Basic OIT pipeline accepted");clean();fail_create=false;
    null_success=true;threw=false;
    try{evrm::NativeDrawPipelines p(device,get,basic,vs,ps,ps);}catch(const std::runtime_error&){threw=true;}
    need(threw,"null-success Basic GBuffer accepted");clean();
    threw=false;try{evrm::NativeOitDrawPipelines p(device,get,basic,vs,ps,true);}catch(const std::runtime_error&){threw=true;}
    need(threw,"null-success Basic OIT accepted");clean();null_success=false;
    std::puts("PASS lifecycle: failed second Basic pipeline, failed OIT, null-success results, complete cleanup");
}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::set_terminate([]() noexcept {std::fprintf(stderr,"TERMINATE native surface draw scenario=%s events=%u creates=%u\n",scenario,event_count,creates);std::fflush(stderr);std::_Exit(98);});
    try{gbuffer();oit();shadow();constructor_failures();need(callbacks_ok,"nonthrowing fake callback reported invalid input");for(const auto& p:pipelines)need(!p.live,"pipeline leaked after owner destruction");need(destroys==creates-4,"pipeline destruction accounting");std::puts("native surface draw: all checks passed (CPU fake functions only)");return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL native surface draw scenario=%s: %s\n",scenario,error.what());return 1;}
}
