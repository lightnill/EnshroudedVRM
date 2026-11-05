#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/native_player_commit.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {
using namespace evrm;
template<class T> T handle(std::uintptr_t n){return reinterpret_cast<T>(n);}
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::uintptr_t serial=100;
unsigned created=0,destroyed=0,allocation_calls=0;
unsigned fail_at=0;
const char* missing=nullptr;
std::set<std::uintptr_t> live;
std::map<VkDescriptorSet,std::array<VkDescriptorBufferInfo,2>> sets;
std::map<VkDescriptorPool,std::vector<VkDescriptorSet>> pools;
std::map<VkBuffer,std::vector<std::uint32_t>> buffers;
std::vector<std::uint32_t> words;
std::vector<std::string> events;
std::string scenario="startup";
unsigned current_opcode=0;
void marker(const char* at){std::fprintf(stderr,"scenario=%s boundary=%s opcode=%u\n",scenario.c_str(),at,current_opcode);std::fflush(stderr);}
[[noreturn]] void fatal_exit(const char* why){
    std::fprintf(stderr,"fixture fatal=%s scenario=%s opcode=%u stack-rvas:",why,scenario.c_str(),current_opcode);
    void* stack[20]{};const auto count=CaptureStackBackTrace(0,20,stack,nullptr);
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    for(unsigned n=0;n<count;++n){const auto address=reinterpret_cast<std::uintptr_t>(stack[n]);if(address>=base&&address<base+0x1000000)std::fprintf(stderr," %llx",static_cast<unsigned long long>(address-base));}
    std::fputs("\n",stderr);std::fflush(stderr);std::_Exit(98);
}
VkDescriptorSet bound{};
std::uint32_t mode=99;
unsigned dispatch_count=0,barrier_count=0;
bool fail(){return ++allocation_calls==fail_at;}
template<class T> VkResult make(T* out){if(fail())return VK_ERROR_OUT_OF_HOST_MEMORY;*out=handle<T>(++serial);live.insert(serial);++created;return VK_SUCCESS;}
template<class T> void erase(T object){require(live.erase(reinterpret_cast<std::uintptr_t>(object))==1,"invalid or repeated resource destruction");++destroyed;}
VkResult VKAPI_CALL create_dsl(VkDevice,const VkDescriptorSetLayoutCreateInfo* i,const VkAllocationCallbacks*,VkDescriptorSetLayout* out){
    require(i->bindingCount==2,"core minimum permits only two storage bindings");
    for(unsigned n=0;n<2;++n)require(i->pBindings[n].binding==n&&i->pBindings[n].descriptorCount==1&&i->pBindings[n].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&i->pBindings[n].stageFlags==VK_SHADER_STAGE_COMPUTE_BIT,"invalid storage binding");
    return make(out);
}
VkResult VKAPI_CALL create_layout(VkDevice,const VkPipelineLayoutCreateInfo* i,const VkAllocationCallbacks*,VkPipelineLayout* out){
    require(i->setLayoutCount==1&&i->pushConstantRangeCount==1,"private layout shape");
    require(i->pPushConstantRanges[0].size==4&&i->pPushConstantRanges[0].offset==0&&i->pPushConstantRanges[0].stageFlags==VK_SHADER_STAGE_COMPUTE_BIT,"mode push constant shape");return make(out);
}
VkResult VKAPI_CALL create_shader(VkDevice,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks*,VkShaderModule* out){
    require(i->codeSize==words.size()*4&&!std::memcmp(i->pCode,words.data(),i->codeSize),"pipeline did not receive the compiled shader");return make(out);
}
VkResult VKAPI_CALL create_pipeline(VkDevice,VkPipelineCache,unsigned count,const VkComputePipelineCreateInfo* i,const VkAllocationCallbacks*,VkPipeline* out){
    require(count==1&&i->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT&&!std::strcmp(i->stage.pName,"main"),"compute entry point");return make(out);
}
VkResult VKAPI_CALL create_pool(VkDevice,const VkDescriptorPoolCreateInfo* i,const VkAllocationCallbacks*,VkDescriptorPool* out){
    require(i->maxSets>=1&&i->maxSets<=16&&i->poolSizeCount==1,"bounded per-batch pool");
    require(i->pPoolSizes[0].type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&i->pPoolSizes[0].descriptorCount==i->maxSets*2,"two descriptors per subject");
    const auto r=make(out);if(r==VK_SUCCESS)pools[*out]={};return r;
}
void VKAPI_CALL destroy_dsl(VkDevice,VkDescriptorSetLayout h,const VkAllocationCallbacks*){erase(h);}
void VKAPI_CALL destroy_layout(VkDevice,VkPipelineLayout h,const VkAllocationCallbacks*){erase(h);}
void VKAPI_CALL destroy_shader(VkDevice,VkShaderModule h,const VkAllocationCallbacks*){erase(h);}
void VKAPI_CALL destroy_pipeline(VkDevice,VkPipeline h,const VkAllocationCallbacks*){erase(h);}
void VKAPI_CALL destroy_pool(VkDevice,VkDescriptorPool h,const VkAllocationCallbacks*){
    for(const auto s:pools.at(h))sets.erase(s);
    pools.erase(h);erase(h);
}
VkResult VKAPI_CALL allocate_sets(VkDevice,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* out){
    if(fail())return VK_ERROR_OUT_OF_POOL_MEMORY;
    require(i->descriptorSetCount>=1&&i->descriptorSetCount<=16,"unbounded sets");
    for(unsigned n=0;n<i->descriptorSetCount;++n){out[n]=handle<VkDescriptorSet>(++serial);sets[out[n]]={};pools.at(i->descriptorPool).push_back(out[n]);}return VK_SUCCESS;
}
void VKAPI_CALL update_sets(VkDevice,unsigned count,const VkWriteDescriptorSet* w,unsigned copies,const VkCopyDescriptorSet*){
    require(copies==0&&count>=2&&count<=32,"unexpected descriptor update");
    for(unsigned n=0;n<count;++n){require(w[n].dstBinding<2&&w[n].descriptorCount==1&&w[n].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,"invalid descriptor write");sets.at(w[n].dstSet)[w[n].dstBinding]=*w[n].pBufferInfo;}
}
// Execute the actual DXC SPIR-V subset against owned fixture buffers. Unknown
// opcodes fail; this is deliberately not a general Vulkan/GPU emulator.
void execute_shader(const std::array<VkDescriptorBufferInfo,2>& descriptors){
    marker("shader metadata begin");
    struct Pointer {unsigned binding=9;std::size_t index=0;};
    std::map<unsigned,std::uint32_t> values;
    std::map<unsigned,Pointer> pointers;
    std::map<unsigned,unsigned> bindings;
    std::map<unsigned,std::size_t> labels;
    std::size_t entry=0;
    for(std::size_t at=5;at<words.size();at+=words[at]>>16){
        const auto count=words[at]>>16;require(count&&count<=words.size()-at,"malformed shader instruction length");
        // A final one-word OpFunctionEnd has zero operands. Forming its valid
        // one-past pointer must not call vector::operator[](size()).
        const auto op=words[at]&65535;const auto* a=words.data()+at+1;
        if(op==71&&a[1]==33)bindings[a[0]]=a[2];
        if(op==43)values[a[1]]=a[2];
        if(op==59){if(a[2]==9)pointers[a[1]]={2,0};else if(a[2]==2||a[2]==12)pointers[a[1]]={bindings.at(a[1]),0};}
        if(op==54)entry=at;
        if(op==248)labels[a[0]]=at;
    }
    auto load=[&](const Pointer& p){if(p.binding==2)return mode;const auto& d=descriptors.at(p.binding);return buffers.at(d.buffer).at(std::size_t(d.offset/4)+p.index);};
    auto store=[&](const Pointer& p,std::uint32_t value){require(p.binding<2,"write into push constants");const auto& d=descriptors.at(p.binding);require(p.index*4+4<=d.range,"shader descriptor out of range");buffers.at(d.buffer).at(std::size_t(d.offset/4)+p.index)=value;};
    marker("shader metadata end");
    unsigned steps=0;
    for(std::size_t at=entry;at<words.size();){
        require(++steps<1000,"shader loop budget");const auto op=words[at]&65535;const auto count=words[at]>>16;require(count&&count<=words.size()-at,"malformed executed instruction length");const auto* a=words.data()+at+1;
        current_opcode=op;
        switch(op){
        case 54:case 248:case 247:break;
        case 65:{auto p=pointers.at(a[2]);for(unsigned n=3;n<count-1;++n)p.index+=values.at(a[n]);pointers[a[1]]=p;break;}
        case 61:values[a[1]]=load(pointers.at(a[2]));break;
        case 62:store(pointers.at(a[0]),values.at(a[1]));break;
        case 170:values[a[1]]=values.at(a[2])==values.at(a[3]);break;
        case 169:values[a[1]]=values.at(a[2])?values.at(a[3]):values.at(a[4]);break;
        case 240:{const auto p=pointers.at(a[2]);values[a[1]]=load(p);store(p,values[a[1]]&values.at(a[5]));break;}
        case 249:at=labels.at(a[0]);continue;
        case 250:at=labels.at(values.at(a[0])?a[1]:a[2]);continue;
        case 253:return;
        default:throw std::runtime_error("unsupported emitted shader opcode "+std::to_string(op));
        }
        at+=count;
    }
    throw std::runtime_error("shader did not return");
}
void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags,unsigned count,const VkMemoryBarrier* b,unsigned buffer_count,const VkBufferMemoryBarrier*,unsigned image_count,const VkImageMemoryBarrier*){
    marker("barrier entry");
    require(count==1&&buffer_count==0&&image_count==0&&dst==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,"common compute barrier shape");
    if(barrier_count==0){require((src&(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))==(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),"host initialization/prepare stages missing");require((b->srcAccessMask&(VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT))==(VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT),"prior prepare/host writes missing");require((b->dstAccessMask&(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT))==(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT),"aggregate read/write dependency missing");events.push_back("ready");}
    else{require(barrier_count==1&&src==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&(b->srcAccessMask&VK_ACCESS_SHADER_WRITE_BIT)&&(b->dstAccessMask&VK_ACCESS_SHADER_READ_BIT),"aggregate write/read dependency missing");events.push_back("aggregate");}
    ++barrier_count;marker("barrier exit");
}
void VKAPI_CALL bind_pipeline(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline h){marker("pipeline bind");require(point==VK_PIPELINE_BIND_POINT_COMPUTE&&live.count(reinterpret_cast<std::uintptr_t>(h)),"dead compute pipeline");events.push_back("pipeline");}
void VKAPI_CALL bind_sets(VkCommandBuffer,VkPipelineBindPoint point,VkPipelineLayout,unsigned first,unsigned count,const VkDescriptorSet* s,unsigned dynamic,const unsigned*){marker("sets bind");require(point==VK_PIPELINE_BIND_POINT_COMPUTE&&first==0&&count==1&&!dynamic,"private descriptor set bind");bound=*s;}
void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags stages,unsigned offset,unsigned size,const void* p){marker("push mode");require(stages==VK_SHADER_STAGE_COMPUTE_BIT&&!offset&&size==4,"invalid push mode");std::memcpy(&mode,p,4);require(mode<=1,"unknown mode");}
void VKAPI_CALL dispatch(VkCommandBuffer,unsigned x,unsigned y,unsigned z){
    marker("dispatch entry");
    require(x==1&&y==1&&z==1,"single invocation required");require(barrier_count==(mode?2u:1u),"dispatch before appropriate memory barrier");
    events.push_back(mode?"propagate":"reduce");++dispatch_count;execute_shader(sets.at(bound));
}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name){
    if(missing&&!std::strcmp(name,missing))return nullptr;
#define FN(n,f) if(!std::strcmp(name,n))return reinterpret_cast<PFN_vkVoidFunction>(f)
    FN("vkCreateDescriptorSetLayout",create_dsl);FN("vkDestroyDescriptorSetLayout",destroy_dsl);
    FN("vkCreatePipelineLayout",create_layout);FN("vkDestroyPipelineLayout",destroy_layout);
    FN("vkCreateShaderModule",create_shader);FN("vkDestroyShaderModule",destroy_shader);
    FN("vkCreateComputePipelines",create_pipeline);FN("vkDestroyPipeline",destroy_pipeline);
    FN("vkCreateDescriptorPool",create_pool);FN("vkDestroyDescriptorPool",destroy_pool);
    FN("vkAllocateDescriptorSets",allocate_sets);FN("vkUpdateDescriptorSets",update_sets);
    FN("vkCmdPipelineBarrier",barrier);FN("vkCmdBindPipeline",bind_pipeline);
    FN("vkCmdBindDescriptorSets",bind_sets);FN("vkCmdPushConstants",push);FN("vkCmdDispatch",dispatch);
#undef FN
    return nullptr;
}
VkDevice device(){return handle<VkDevice>(1);}
VkCommandBuffer command(){return handle<VkCommandBuffer>(2);}
VkDescriptorBufferInfo buffer(unsigned permit,bool frame=true,VkDeviceSize offset=0){
    const auto h=handle<VkBuffer>(++serial);buffers[h]=std::vector<std::uint32_t>(std::size_t(offset/4)+64,0x12345678);buffers[h][std::size_t(offset/4)+(frame?48:0)]=permit;return {h,offset,frame?196u:4u};
}
void reset_commands(){events.clear();dispatch_count=barrier_count=0;bound={};mode=99;}
void check_common_permits(unsigned count,int rejected,unsigned initial,unsigned expected){
    scenario="common permits count="+std::to_string(count)+" rejected="+std::to_string(rejected)+" initial="+std::to_string(initial);
    marker("before helper construction");
    NativePlayerCommit helper(device(),get,words.data(),words.size());
    marker("after helper construction");
    std::vector<VkDescriptorBufferInfo> frames;
    for(unsigned n=0;n<count;++n)frames.push_back(buffer(int(n)==rejected?0:1,true,n==0?256:0));
    const auto gate=buffer(initial,false);auto batch=helper.batch(frames,gate);require(bool(batch),"valid batch rejected");
    marker("after batch construction");
    require(buffers.at(gate.buffer)[0]==initial,"batch construction wrote host gate");
    reset_commands();require(helper.record(command(),*batch),"record valid batch failed");
    marker("after record");
    require(dispatch_count==count*2&&barrier_count==2,"two phases not recorded for all subjects");
    const auto aggregate=std::find(events.begin(),events.end(),"aggregate");
    require(unsigned(std::count(events.begin(),aggregate,"reduce"))==count&&std::count(events.begin(),aggregate,"propagate")==0,"propagation preceded full aggregate");
    require(buffers.at(gate.buffer)[0]==expected,"common clip gate disagrees with acceptance");
    for(const auto& frame:frames){const auto& b=buffers.at(frame.buffer);require(b[std::size_t(frame.offset/4)+48]==expected,"frame offset192 disagrees with common clip gate");require(b[std::size_t(frame.offset/4)+47]==0x12345678&&b[std::size_t(frame.offset/4)+49]==0x12345678,"frame bytes outside permit changed");}
    marker("before destruction");
}
void validation(){
    NativePlayerCommit helper(device(),get,words.data(),words.size());auto frame=buffer(1);auto gate=buffer(1,false);
    require(!helper.batch({},gate),"empty batch accepted");
    require(!helper.batch(std::vector<VkDescriptorBufferInfo>(17,frame),gate),"17 players accepted");
    require(!helper.batch({frame,frame},gate),"duplicate frame buffers accepted");
    require(!helper.batch({frame},frame),"aliased gate accepted");
    for(const auto bad:std::vector<VkDescriptorBufferInfo>{{VK_NULL_HANDLE,0,196},{frame.buffer,0,192},{frame.buffer,4,196},{frame.buffer,0,VK_WHOLE_SIZE},{frame.buffer,std::numeric_limits<VkDeviceSize>::max()-255,512},{frame.buffer,0,197}})require(!helper.batch({bad},gate),"malformed frame descriptor accepted");
    for(const auto bad:std::vector<VkDescriptorBufferInfo>{{VK_NULL_HANDLE,0,4},{gate.buffer,0,3},{gate.buffer,4,4},{gate.buffer,0,VK_WHOLE_SIZE}})require(!helper.batch({frame},bad),"malformed gate descriptor accepted");
    auto batch=helper.batch({frame},gate);require(bool(batch),"valid fixture");reset_commands();require(!helper.record(VK_NULL_HANDLE,*batch)&&events.empty(),"null command recorded");
    NativePlayerCommit other(device(),get,words.data(),words.size());require(!other.record(command(),*batch)&&events.empty(),"foreign batch recorded");
}
void failed_resources(){
    const auto before=live.size();
    for(unsigned n=1;n<=4;++n){allocation_calls=0;fail_at=n;bool threw=false;marker("before failed constructor");try{NativePlayerCommit helper(device(),get,words.data(),words.size());}catch(const std::exception&){threw=true;}marker("after failed constructor");require(threw&&live.size()==before,"failed constructor leaked or did not throw");}
    fail_at=0;allocation_calls=0;
    missing="vkCmdPushConstants";bool threw=false;try{NativePlayerCommit helper(device(),get,words.data(),words.size());}catch(const std::exception&){threw=true;}missing=nullptr;require(threw&&live.size()==before,"missing driver entry accepted or leaked");
    NativePlayerCommit helper(device(),get,words.data(),words.size());const auto ready=live.size();
    for(unsigned n=1;n<=2;++n){allocation_calls=0;fail_at=n;require(!helper.batch({buffer(1)},buffer(1,false)),"failed batch allocation accepted");require(live.size()==ready,"failed batch allocation leaked");}
    fail_at=0;allocation_calls=0;
}
void lifetime_and_quarantine(){
    auto helper=std::make_unique<NativePlayerCommit>(device(),get,words.data(),words.size());auto batch=helper->batch({buffer(1)},buffer(1,false));require(bool(batch),"lifetime batch");
    const auto retained=live.size();helper.reset();require(live.size()==retained,"pipeline destroyed before retained batch retirement");batch.reset();require(live.empty(),"normal batch retirement leaked");
    for(const bool through_batch:{false,true}){
        helper=std::make_unique<NativePlayerCommit>(device(),get,words.data(),words.size());batch=helper->batch({buffer(1)},buffer(1,false));const auto sibling=helper->batch({buffer(1)},buffer(1,false));require(bool(batch)&&bool(sibling),"quarantine fixture allocation");
        const auto before=destroyed;if(through_batch)batch->quarantine();else helper->quarantine();
        reset_commands();require(!helper->record(command(),*batch)&&events.empty(),"quarantined batch recorded");require(!helper->record(command(),*sibling)&&events.empty(),"shared uncertain pipeline reused");require(!helper->batch({buffer(1)},buffer(1,false)),"quarantined helper allocated batch");
        batch.reset();helper.reset();require(destroyed==before,"uncertain resources destroyed");
    }
}
}
int main(int argc,char** argv){
    // Fake Vulkan checks may throw through the production noexcept record API.
    // Report that contract failure directly, without CRT abort/WER desktop UI.
    std::set_terminate([]{
        std::fprintf(stderr,"fixture terminated scenario=%s opcode=%u exception=",scenario.c_str(),current_opcode);
        try{const auto e=std::current_exception();if(e)std::rethrow_exception(e);}
        catch(const std::exception& e){std::fputs(e.what(),stderr);}
        catch(...){std::fputs("unknown exception",stderr);}
        std::fputs("\n",stderr);std::fflush(stderr);std::_Exit(97);
    });
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::signal(SIGABRT,[](int){fatal_exit("SIGABRT");});
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG{
        if(e&&e->ExceptionRecord&&e->ExceptionRecord->ExceptionCode==0x40000015)fatal_exit("0x40000015");
        return EXCEPTION_CONTINUE_SEARCH;
    });
    // This w64devkit's msvcrt import library omits _set_abort_behavior. Use it
    // only when exported by an already loaded CRT; never load another runtime.
    for(const auto* name:{L"msvcrt.dll",L"ucrtbase.dll"})if(const auto module=GetModuleHandleW(name)){
        using AbortBehavior=unsigned(__cdecl*)(unsigned,unsigned);
        const auto address=GetProcAddress(module,"_set_abort_behavior");AbortBehavior suppress{};
        static_assert(sizeof(suppress)==sizeof(address),"Windows function pointer shape");
        std::memcpy(&suppress,&address,sizeof(suppress));if(suppress)suppress(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    }
    std::fputs("scenario=startup\n",stderr);std::fflush(stderr);
    try{
        require(argc==2,"compiled shader path required");std::ifstream file(argv[1],std::ios::binary|std::ios::ate);require(bool(file),"shader open failed");const auto bytes=file.tellg();require(bytes>=20&&bytes%4==0,"shader shape");words.resize(std::size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),bytes);require(words[0]==0x07230203,"SPIR-V header");
        check_common_permits(1,-1,1,1);check_common_permits(2,-1,1,1);check_common_permits(2,1,1,0);check_common_permits(16,-1,1,1);check_common_permits(16,15,1,0);check_common_permits(16,0,1,0);check_common_permits(2,-1,0,0);
        scenario="validation";marker("begin");validation();marker("end");scenario="failed resources";marker("begin");failed_resources();marker("end");require(live.empty(),"normal tests leaked resources");scenario="lifetime/quarantine";marker("begin");lifetime_and_quarantine();marker("end");
        std::cout<<"native player commit: all checks passed (compiled SPIR-V, 1/2/16 subjects, validation, failures, lifetime, quarantine)\n";
    }catch(const std::exception& e){std::fprintf(stderr,"scenario=%s exception=%s\n",scenario.c_str(),e.what());return 1;}
}
