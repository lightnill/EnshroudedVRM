#include "native_player_commit.hpp"
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace evrm { namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void checked(VkResult r,const char* why){require(r==VK_SUCCESS,why);}
bool valid(VkDescriptorBufferInfo b,VkDeviceSize minimum){
    return b.buffer&&b.range>=minimum&&b.range!=VK_WHOLE_SIZE&&b.offset%256==0&&b.range%4==0&&b.range<=std::numeric_limits<VkDeviceSize>::max()-b.offset;
}
}
#define COMMIT_FUNCTIONS(X) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
X(vkCreateShaderModule) X(vkDestroyShaderModule) \
X(vkCreateComputePipelines) X(vkDestroyPipeline) \
X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDispatch)

struct NativePlayerCommitState {
    VkDevice device{};
    std::atomic<bool> uncertain{false};
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
#define DECLARE(n) PFN_##n n{};
    COMMIT_FUNCTIONS(DECLARE)
#undef DECLARE
    ~NativePlayerCommitState(){
        if(uncertain.load())return;
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(shader)vkDestroyShaderModule(device,shader,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(dsl)vkDestroyDescriptorSetLayout(device,dsl,nullptr);
    }
};
struct NativePlayerCommitBatch::Impl {
    std::shared_ptr<NativePlayerCommitState> state;
    VkDescriptorPool pool{};
    std::vector<VkDescriptorSet> sets;
    ~Impl(){if(pool&&!state->uncertain.load())state->vkDestroyDescriptorPool(state->device,pool,nullptr);}
};
NativePlayerCommitBatch::NativePlayerCommitBatch(std::unique_ptr<Impl> p):impl(std::move(p)){}
NativePlayerCommitBatch::~NativePlayerCommitBatch()=default;
void NativePlayerCommitBatch::quarantine()noexcept{if(impl)impl->state->uncertain.store(true);}

NativePlayerCommit::NativePlayerCommit(VkDevice device,PFN_vkGetDeviceProcAddr get,const std::uint32_t* words,std::size_t word_count){
    require(device&&get&&words&&word_count>=5&&word_count<=4*1024*1024,"Player commit shader/driver inputs");
    require(words[0]==0x07230203,"Player commit shader header");
    auto state=std::make_shared<NativePlayerCommitState>();auto& p=*state;p.device=device;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"Player commit missing driver entry");
    COMMIT_FUNCTIONS(LOAD)
#undef LOAD
    const std::array<VkDescriptorSetLayoutBinding,2> bindings={{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
    VkDescriptorSetLayoutCreateInfo dsl{};dsl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dsl.bindingCount=bindings.size();dsl.pBindings=bindings.data();
    checked(p.vkCreateDescriptorSetLayout(device,&dsl,nullptr,&p.dsl),"Player commit descriptor layout");
    const VkPushConstantRange mode={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&p.dsl;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&mode;
    checked(p.vkCreatePipelineLayout(device,&layout,nullptr,&p.layout),"Player commit pipeline layout");
    VkShaderModuleCreateInfo shader{};shader.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;shader.codeSize=word_count*4;shader.pCode=words;
    checked(p.vkCreateShaderModule(device,&shader,nullptr,&p.shader),"Player commit shader");
    VkComputePipelineCreateInfo pipeline{};pipeline.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;pipeline.layout=p.layout;pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=p.shader;pipeline.stage.pName="main";
    checked(p.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"Player commit compute pipeline");
    impl=std::move(state);
}
NativePlayerCommit::~NativePlayerCommit()=default;
std::shared_ptr<NativePlayerCommitBatch> NativePlayerCommit::batch(const std::vector<VkDescriptorBufferInfo>& frames,VkDescriptorBufferInfo gate){
    if(!impl||impl->uncertain.load()||frames.empty()||frames.size()>16||!valid(gate,4))return {};
    for(std::size_t n=0;n<frames.size();++n){
        if(!valid(frames[n],196)||frames[n].buffer==gate.buffer)return {};
        for(std::size_t prior=0;prior<n;++prior)if(frames[prior].buffer==frames[n].buffer)return {};
    }
    try{
        auto batch=std::make_unique<NativePlayerCommitBatch::Impl>();batch->state=impl;auto& p=*impl;
        const auto count=static_cast<std::uint32_t>(frames.size());
        const VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,count*2};
        VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=count;pool.poolSizeCount=1;pool.pPoolSizes=&size;
        checked(p.vkCreateDescriptorPool(p.device,&pool,nullptr,&batch->pool),"Player commit descriptor pool");
        batch->sets.resize(count);std::array<VkDescriptorSetLayout,16> layouts{};layouts.fill(p.dsl);
        VkDescriptorSetAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocate.descriptorPool=batch->pool;allocate.descriptorSetCount=count;allocate.pSetLayouts=layouts.data();
        checked(p.vkAllocateDescriptorSets(p.device,&allocate,batch->sets.data()),"Player commit descriptor sets");
        std::array<VkWriteDescriptorSet,32> writes{};
        for(std::uint32_t n=0;n<count;++n)for(unsigned binding=0;binding<2;++binding){auto& w=writes[n*2+binding];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=batch->sets[n];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=binding?&gate:&frames[n];}
        p.vkUpdateDescriptorSets(p.device,count*2,writes.data(),0,nullptr);
        return std::shared_ptr<NativePlayerCommitBatch>(new NativePlayerCommitBatch(std::move(batch)));
    }catch(...){return {};}
}
bool NativePlayerCommit::record(VkCommandBuffer command,const NativePlayerCommitBatch& batch)noexcept{
    if(!command||!impl||impl->uncertain.load()||!batch.impl||batch.impl->state!=impl||batch.impl->sets.empty())return false;
    auto& p=*impl;
    VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
    for(std::uint32_t mode=0;mode<2;++mode){
        if(mode){barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);}
        p.vkCmdPushConstants(command,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&mode);
        for(const auto set:batch.impl->sets){p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&set,0,nullptr);p.vkCmdDispatch(command,1,1,1);}
    }
    return true;
}
void NativePlayerCommit::quarantine()noexcept{if(impl)impl->uncertain.store(true);}
#undef COMMIT_FUNCTIONS
}
