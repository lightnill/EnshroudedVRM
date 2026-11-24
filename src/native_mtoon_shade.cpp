#include "native_mtoon_shade.hpp"
#include <atomic>
#include <algorithm>
#include "native_mtoon_forward.hpp"
#include <stdexcept>
#include <vector>
namespace evrm {namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void checked(VkResult value,const char* why){require(value==VK_SUCCESS,why);}
bool power(VkDeviceSize n){return n&&!(n&(n-1));}
bool span(VkDescriptorBufferInfo b,VkDeviceSize need,VkDeviceSize limit,VkDeviceSize alignment){
    return b.buffer&&b.range!=VK_WHOLE_SIZE&&b.range>=need&&b.range<=limit&&power(alignment)&&
        b.offset%alignment==0&&b.offset<=~VkDeviceSize(0)-b.range;
}
bool overlap(VkDescriptorBufferInfo a,VkDescriptorBufferInfo b){
    return a.buffer==b.buffer&&a.offset<b.offset+b.range&&b.offset<a.offset+a.range;
}
bool sampled(VkDescriptorImageInfo image){
    return image.imageView&&(image.imageLayout==VK_IMAGE_LAYOUT_GENERAL||
        image.imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||
        image.imageLayout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL||
        image.imageLayout==VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL||
        image.imageLayout==VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL||
        image.imageLayout==VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL||
        image.imageLayout==VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL);
}
void capabilities(const NativeMToonShadeCapabilities& caps){
    const auto& l=caps.limits;
    require(caps.descriptor_binding_partially_bound_enabled,"MToon shade requires already-enabled partially bound descriptors");
    require(l.maxBoundDescriptorSets>=3&&l.maxUniformBufferRange>=2048&&
        l.maxDescriptorSetUniformBuffers>=8&&l.maxPerStageDescriptorUniformBuffers>=8&&
        l.maxDescriptorSetStorageBuffers>=15&&l.maxPerStageDescriptorStorageBuffers>=15&&
        l.maxDescriptorSetSampledImages>=22&&l.maxPerStageDescriptorSampledImages>=22&&
        l.maxDescriptorSetSamplers>=18&&l.maxPerStageDescriptorSamplers>=18&&l.maxPerStageResources>=63&&
        l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=8&&
        l.maxComputeWorkGroupSize[1]>=8&&l.maxComputeWorkGroupSize[2]&&
        l.maxComputeWorkGroupCount[0]&&l.maxComputeWorkGroupCount[1]&&l.maxComputeWorkGroupCount[2],
        "MToon shade program limits");
    if(caps.forward)require(l.maxUniformBufferRange>=2320&&l.maxDescriptorSetUniformBuffers>=9&&l.maxPerStageDescriptorUniformBuffers>=9&&
        l.maxDescriptorSetSampledImages>=23&&l.maxPerStageDescriptorSampledImages>=23&&l.maxDescriptorSetSamplers>=19&&l.maxPerStageDescriptorSamplers>=19&&l.maxPerStageResources>=66,
        "MToon forward shade limits");
}
}
namespace {
struct ShadeBank {
    VkDevice device{};bool uncertain=false;VkDescriptorPool pool{};std::array<VkDescriptorSet,3> sets{};
    PFN_vkDestroyDescriptorPool destroy{};
    ~ShadeBank(){if(!uncertain&&pool)destroy(device,pool,nullptr);}
};
}
struct NativeMToonShadeProgram::Impl {
    VkDevice device{};const bool* parent=nullptr;std::atomic<bool> uncertain{false};
    bool forward=false;std::array<VkDescriptorSetLayout,3> sets{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
    PFN_vkDestroyDescriptorSetLayout destroy_set{};PFN_vkDestroyPipelineLayout destroy_layout{};
    PFN_vkDestroyShaderModule destroy_shader{};PFN_vkDestroyPipeline destroy_pipeline{};
    // Serialized pass owner only. A bank is reusable after the parent has
    // proven original retirement and released its final pass lease. Cached
    // sets are dormant; every binding is freshly written before the next use.
    std::vector<std::shared_ptr<ShadeBank>> banks;
    ~Impl(){if(uncertain){for(auto& bank:banks)bank->uncertain=true;return;}
        banks.clear();if(pipeline)destroy_pipeline(device,pipeline,nullptr);
        if(shader)destroy_shader(device,shader,nullptr);if(layout)destroy_layout(device,layout,nullptr);
        for(auto set:sets)if(set)destroy_set(device,set,nullptr);}
};
NativeMToonShadeProgram::NativeMToonShadeProgram(VkDevice device,PFN_vkGetDeviceProcAddr get,
    const NativeMToonShadeCapabilities& caps,NativeMToonShadeShader shader,const bool* quarantine):impl(std::make_unique<Impl>()){
    require(device&&get&&quarantine&&shader.words&&shader.count>=5&&shader.count<=262144&&
        shader.words[0]==0x07230203,"MToon shade program inputs");capabilities(caps);
    auto& p=*impl;p.device=device;p.parent=quarantine;p.forward=caps.forward;
    p.destroy_set=reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(get(device,"vkDestroyDescriptorSetLayout"));
    p.destroy_layout=reinterpret_cast<PFN_vkDestroyPipelineLayout>(get(device,"vkDestroyPipelineLayout"));
    p.destroy_shader=reinterpret_cast<PFN_vkDestroyShaderModule>(get(device,"vkDestroyShaderModule"));
    p.destroy_pipeline=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));
    const auto create_set=reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(get(device,"vkCreateDescriptorSetLayout"));
    const auto create_layout=reinterpret_cast<PFN_vkCreatePipelineLayout>(get(device,"vkCreatePipelineLayout"));
    const auto create_shader=reinterpret_cast<PFN_vkCreateShaderModule>(get(device,"vkCreateShaderModule"));
    const auto create_pipeline=reinterpret_cast<PFN_vkCreateComputePipelines>(get(device,"vkCreateComputePipelines"));
    require(p.destroy_set&&p.destroy_layout&&p.destroy_shader&&p.destroy_pipeline&&create_set&&create_layout&&
        create_shader&&create_pipeline,"MToon shade program driver entries");
    const auto material=native_mtoon_shade_material_bindings();const auto owned=native_mtoon_shade_owned_bindings();
    const auto scene=native_mtoon_shade_scene_bindings();
    const auto make_set=[&](unsigned index,const auto& bindings,bool partial){
        std::array<VkDescriptorBindingFlags,26> flags{};flags.fill(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
        VkDescriptorSetLayoutBindingFlagsCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        fi.bindingCount=static_cast<std::uint32_t>(bindings.size());fi.pBindingFlags=flags.data();
        VkDescriptorSetLayoutCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        si.pNext=partial?&fi:nullptr;si.bindingCount=fi.bindingCount;si.pBindings=bindings.data();
        checked(create_set(device,&si,nullptr,&p.sets[index]),"MToon shade set layout");require(bool(p.sets[index]),"MToon shade null set layout");
    };
    make_set(0,material,false);
    if(caps.forward){std::array<VkDescriptorSetLayoutBinding,17> forward{};std::copy(owned.begin(),owned.end(),forward.begin());
        forward[14]={14,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        forward[15]={15,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        forward[16]={16,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};make_set(1,forward,false);
    }else make_set(1,owned,false);make_set(2,scene,true);
    VkPipelineLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=3;li.pSetLayouts=p.sets.data();
    checked(create_layout(device,&li,nullptr,&p.layout),"MToon shade pipeline layout");require(bool(p.layout),"MToon shade null pipeline layout");
    VkShaderModuleCreateInfo mi{};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.codeSize=shader.count*4;mi.pCode=shader.words;
    checked(create_shader(device,&mi,nullptr,&p.shader),"MToon shade shader");require(bool(p.shader),"MToon shade null shader");
    VkComputePipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;ci.layout=p.layout;
    ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module=p.shader;ci.stage.pName="cs_shade";
    checked(create_pipeline(device,VK_NULL_HANDLE,1,&ci,nullptr,&p.pipeline),"MToon shade pipeline");require(bool(p.pipeline),"MToon shade null pipeline");
}
NativeMToonShadeProgram::~NativeMToonShadeProgram()=default;
void NativeMToonShadeProgram::quarantine()const noexcept{if(impl)impl->uncertain=true;}
bool NativeMToonShadeProgram::reusable()const noexcept{return impl&&!impl->uncertain;}
#define SHADE_FUNCTIONS(X) \
X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatchIndirect)
struct NativeMToonShadePass::Impl {
    VkDevice device{};const bool* parent=nullptr;bool uncertain=false,recorded=false;
    std::shared_ptr<const NativeMToonShadeProgram> program;std::atomic<bool>* program_uncertain=nullptr;
    std::shared_ptr<ShadeBank> bank;std::array<VkDescriptorSet,3> sets{};VkBuffer indirect{};VkDeviceSize offset=0;
    std::uint32_t set_limit=0,forward_groups=0;PFN_vkCmdDispatch direct{};
#define DECLARE(n) PFN_##n n{};
    SHADE_FUNCTIONS(DECLARE)
#undef DECLARE
    bool quarantined()const noexcept{return uncertain||(parent&&*parent)||(program_uncertain&&program_uncertain->load());}
    ~Impl(){if(quarantined()){if(program_uncertain)program_uncertain->store(true);if(bank)bank->uncertain=true;}}
};
NativeMToonShadePass::NativeMToonShadePass(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonShadeCapabilities& caps,
    NativeMToonShadeParameters shape,const NativeMToonShadeInputs& inputs,const bool* quarantine,
    std::shared_ptr<const NativeMToonShadeProgram> prepared){
    const auto pixels=std::uint64_t(shape.width)*shape.height;const auto& l=caps.limits;
    require(device&&get&&quarantine&&!*quarantine&&prepared&&prepared->impl&&prepared->impl->device==device&&
        prepared->impl->parent==quarantine&&!prepared->impl->uncertain,"MToon shade prepared-program owner");
    capabilities(caps);
    require(shape.width&&shape.height&&shape.width<=16384&&shape.height<=16384&&pixels<=pixel_limit&&
        shape.generation&&shape.work_index<draw_limit&&l.maxComputeWorkGroupCount[0]>=(shape.width+7u)/8u&&
        l.maxComputeWorkGroupCount[1]>=(shape.height+7u)/8u,"MToon shade shape/work limits");
    require(prepared->impl->forward==caps.forward,"MToon shade program family mismatch");
    const auto forward=native_mtoon_forward_layout(shape.width,shape.height,shape.forward_capacity);
    require(!caps.forward||(bool(forward)&&l.maxComputeWorkGroupSize[0]>=64&&l.maxComputeWorkGroupCount[0]>=(shape.forward_capacity+63)/64),"MToon forward dispatch shape");
    auto material=inputs.material;auto owned=inputs.owned;auto source=inputs.scene_buffers;
    require(span(material,752,l.maxUniformBufferRange,l.minUniformBufferOffsetAlignment),"MToon shade material span");material.range=752;
    std::array<VkDeviceSize,14> minimum{64,96,224,44,12,4,4,4,4,pixels*4,16+pixels*16,(VkDeviceSize(shape.work_index)+1)*32,16,4};
    if(caps.forward){minimum[9]=forward.capture_bytes;minimum[10]=forward.packet_bytes;minimum[11]=pixels*4;}
    for(unsigned i=0;i<owned.size();++i){const bool uniform=i<3;
        require(span(owned[i],minimum[i],uniform?l.maxUniformBufferRange:l.maxStorageBufferRange,
            uniform?l.minUniformBufferOffsetAlignment:l.minStorageBufferOffsetAlignment),"MToon shade owned buffer span");
        // Private numeric build may expose a bounded owned packet tail. The
        // shader requires its keyed header before any diagnostic tail access.
        if(uniform||i==9||i==12)owned[i].range=minimum[i];}
    const std::array<VkDeviceSize,8> native_minimum{2048,64,96,640,4,4,4,4};
    for(unsigned i=0;i<source.size();++i){const bool uniform=i<4;
        require(span(source[i],native_minimum[i],uniform?l.maxUniformBufferRange:l.maxStorageBufferRange,
            uniform?l.minUniformBufferOffsetAlignment:l.minStorageBufferOffsetAlignment),"MToon shade source buffer span");
        if(uniform)source[i].range=native_minimum[i];}
    require(!overlap(owned[10],material),"MToon shade output/material overlap");
    for(unsigned i=0;i<owned.size();++i)if(i!=10)require(!overlap(owned[10],owned[i]),"MToon shade output/owned overlap");
    for(const auto& buffer:source)require(!overlap(owned[10],buffer),"MToon shade output/source overlap");
    auto maps=inputs.material_maps;auto images=inputs.scene_images;
    std::array<VkDescriptorImageInfo,11> material_samplers{};std::array<VkDescriptorImageInfo,7> scene_samplers{};
    for(unsigned i=0;i<maps.size();++i){require(sampled(maps[i])&&inputs.material_samplers[i],"MToon shade material image/sampler");
        maps[i].sampler=VK_NULL_HANDLE;material_samplers[i].sampler=inputs.material_samplers[i];}
    for(auto& image:images){require(sampled(image),"MToon shade source image");image.sampler=VK_NULL_HANDLE;}
    for(unsigned i=0;i<scene_samplers.size();++i){require(bool(inputs.scene_samplers[i]),"MToon shade source sampler");scene_samplers[i].sampler=inputs.scene_samplers[i];}
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.parent=quarantine;p.program=std::move(prepared);
    p.program_uncertain=&p.program->impl->uncertain;p.set_limit=l.maxBoundDescriptorSets;
    if(caps.forward){p.direct=reinterpret_cast<PFN_vkCmdDispatch>(get(device,"vkCmdDispatch"));require(p.direct!=nullptr,"MToon forward dispatch entry");p.forward_groups=(shape.forward_capacity+63)/64;}
    p.indirect=owned[11].buffer;p.offset=owned[11].offset+VkDeviceSize(shape.work_index)*32+16;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"MToon shade missing driver entry");
    SHADE_FUNCTIONS(LOAD)
#undef LOAD
    auto& banks=p.program->impl->banks;
    for(const auto& bank:banks)if(bank.use_count()==1&&!bank->uncertain){p.bank=bank;break;}
    if(!p.bank){
        auto bank=std::make_shared<ShadeBank>();bank->device=device;bank->destroy=p.vkDestroyDescriptorPool;
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,caps.forward?9u:8u},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,15},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,caps.forward?23u:22u},{VK_DESCRIPTOR_TYPE_SAMPLER,caps.forward?19u:18u}};
        VkDescriptorPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pi.maxSets=3;pi.poolSizeCount=4;pi.pPoolSizes=sizes;
        checked(p.vkCreateDescriptorPool(device,&pi,nullptr,&bank->pool),"MToon shade descriptor pool");require(bool(bank->pool),"MToon shade null pool");
        VkDescriptorSetAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=bank->pool;
        ai.descriptorSetCount=3;ai.pSetLayouts=p.program->impl->sets.data();
        checked(p.vkAllocateDescriptorSets(device,&ai,bank->sets.data()),"MToon shade descriptor sets");for(auto set:bank->sets)require(bool(set),"MToon shade null set");
        // Bound retained storage; extra concurrent callers retain their own
        // uncached bank, preserving the previous allocation/failure behavior.
        if(banks.size()<3u*draw_limit)banks.push_back(bank);p.bank=std::move(bank);
    }
    p.sets=p.bank->sets;
    std::array<VkWriteDescriptorSet,46> writes{};unsigned index=0;
    const auto write=[&](unsigned set,unsigned binding,VkDescriptorType type,unsigned count,const VkDescriptorBufferInfo* b,const VkDescriptorImageInfo* image){
        auto& w=writes[index++];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=p.sets[set];w.dstBinding=binding;
        w.descriptorType=type;w.descriptorCount=count;w.pBufferInfo=b;w.pImageInfo=image;
    };
    write(0,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,&material,nullptr);write(0,1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,11,nullptr,maps.data());
    write(0,2,VK_DESCRIPTOR_TYPE_SAMPLER,11,nullptr,material_samplers.data());
    for(unsigned i=0;i<owned.size();++i)write(1,i,i<3?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,&owned[i],nullptr);
    for(unsigned i=0;i<source.size();++i)write(2,i,i<4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,&source[i],nullptr);
    for(unsigned i=0;i<images.size();++i)write(2,i+8,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,nullptr,&images[i]);
    for(unsigned i=0;i<scene_samplers.size();++i)write(2,i+19,VK_DESCRIPTOR_TYPE_SAMPLER,1,nullptr,&scene_samplers[i]);
    VkDescriptorImageInfo forward_image=inputs.forward_visibility,forward_sampler{};auto forward_constants=inputs.forward_constants;
    if(caps.forward){require(span(forward_constants,2320,l.maxUniformBufferRange,l.minUniformBufferOffsetAlignment)&&sampled(forward_image)&&inputs.forward_sampler,"MToon forward owned lighting snapshot");
        require(!overlap(owned[10],forward_constants),"MToon forward output/constants overlap");forward_constants.range=2320;forward_image.sampler=VK_NULL_HANDLE;forward_sampler.sampler=inputs.forward_sampler;
        write(1,14,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,&forward_constants,nullptr);write(1,15,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,nullptr,&forward_image);write(1,16,VK_DESCRIPTOR_TYPE_SAMPLER,1,nullptr,&forward_sampler);}
    p.vkUpdateDescriptorSets(device,index,writes.data(),0,nullptr);
}
NativeMToonShadePass::~NativeMToonShadePass()=default;
bool NativeMToonShadePass::record(VkCommandBuffer command,VkPipeline original,VkPipelineLayout compatible,
    std::uint32_t count,const VkDescriptorSet* sets)noexcept{
    if(!impl||impl->recorded||impl->quarantined()||!command||!original||!compatible||!sets||!count||count>impl->set_limit)return false;
    for(unsigned i=0;i<count;++i)if(!sets[i])return false;
    auto& p=*impl;p.recorded=true;const auto& program=*p.program->impl;
    VkMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&b,0,nullptr,0,nullptr);
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,program.pipeline);
    p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,program.layout,0,3,p.sets.data(),0,nullptr);
    if(p.forward_groups)p.direct(command,p.forward_groups,1,1);else p.vkCmdDispatchIndirect(command,p.indirect,p.offset);
    b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,p.forward_groups?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
    p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,original);
    p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,compatible,0,count,sets,0,nullptr);return true;
}
void NativeMToonShadePass::quarantine()noexcept{if(impl){impl->uncertain=true;if(impl->bank)impl->bank->uncertain=true;if(impl->program)impl->program->impl->uncertain=true;}}
#undef SHADE_FUNCTIONS
}
