#include "native_mtoon_composite.hpp"
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace evrm {namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void checked(VkResult value,const char* why){require(value==VK_SUCCESS,why);}
bool power(VkDeviceSize n){return n&&!(n&(n-1));}
bool span(VkDescriptorBufferInfo b,VkDeviceSize need,VkDeviceSize limit,VkDeviceSize alignment){return b.buffer&&b.range!=VK_WHOLE_SIZE&&b.range>=need&&b.range<=limit&&power(alignment)&&b.offset%alignment==0&&b.offset<=~VkDeviceSize(0)-b.range;}
bool overlap(VkDescriptorBufferInfo a,VkDescriptorBufferInfo b){return a.buffer==b.buffer&&a.offset<b.offset+b.range&&b.offset<a.offset+a.range;}
}
#define COMPOSITE_FUNCTIONS(X) \
X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
namespace {
struct CompositeBank {
    VkDevice device{};bool uncertain=false;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped=nullptr;
    VkDescriptorPool pool{};VkDescriptorSet set{};VkDeviceSize observation_offset=0;
    PFN_vkDestroyDescriptorPool destroy_pool{};PFN_vkUnmapMemory unmap{};PFN_vkDestroyBuffer destroy_buffer{};PFN_vkFreeMemory free_memory{};
    ~CompositeBank(){if(uncertain)return;if(pool)destroy_pool(device,pool,nullptr);if(mapped)unmap(device,memory);if(buffer)destroy_buffer(device,buffer,nullptr);if(memory)free_memory(device,memory,nullptr);}
};
}
struct NativeMToonCompositeProgram::Impl {
    VkDevice device{};const bool* parent=nullptr;std::atomic<bool> uncertain{false};VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};
    PFN_vkDestroyDescriptorSetLayout destroy_set{};PFN_vkDestroyPipelineLayout destroy_layout{};PFN_vkDestroyShaderModule destroy_shader{};PFN_vkDestroyPipeline destroy_pipeline{};
    // Serialized pass owner; only a bank with no outstanding pass lease may
    // be refreshed. Parent proves original completion/reset before release.
    std::vector<std::shared_ptr<CompositeBank>> banks;
    ~Impl(){if(uncertain){for(auto& bank:banks)bank->uncertain=true;return;}
        banks.clear();if(pipeline)destroy_pipeline(device,pipeline,nullptr);if(shader)destroy_shader(device,shader,nullptr);if(layout)destroy_layout(device,layout,nullptr);if(dsl)destroy_set(device,dsl,nullptr);}
};
NativeMToonCompositeProgram::NativeMToonCompositeProgram(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonCompositeCapabilities& caps,NativeMToonCompositeShader shader,const bool* quarantine):impl(std::make_unique<Impl>()){
    const auto& l=caps.limits;
    require(device&&get&&quarantine&&caps.storage_image_extended_formats&&shader.words&&shader.count>=5&&shader.count<=262144&&shader.words[0]==0x07230203,"MToon composite program inputs");
    require(l.maxBoundDescriptorSets&&l.maxUniformBufferRange>=16&&l.maxDescriptorSetUniformBuffers>=2&&l.maxPerStageDescriptorUniformBuffers>=2&&l.maxDescriptorSetStorageBuffers>=3&&l.maxPerStageDescriptorStorageBuffers>=3&&l.maxDescriptorSetStorageImages>=3&&l.maxPerStageDescriptorStorageImages>=3&&l.maxPerStageResources>=8&&l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=8&&l.maxComputeWorkGroupSize[1]>=8&&l.maxComputeWorkGroupSize[2]&&l.maxComputeWorkGroupCount[0]&&l.maxComputeWorkGroupCount[1]&&l.maxComputeWorkGroupCount[2],"MToon composite program limits");
    for(auto f:caps.format_features)require(f&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,"MToon composite program formats");
    auto& p=*impl;p.device=device;p.parent=quarantine;
    p.destroy_set=reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(get(device,"vkDestroyDescriptorSetLayout"));p.destroy_layout=reinterpret_cast<PFN_vkDestroyPipelineLayout>(get(device,"vkDestroyPipelineLayout"));p.destroy_shader=reinterpret_cast<PFN_vkDestroyShaderModule>(get(device,"vkDestroyShaderModule"));p.destroy_pipeline=reinterpret_cast<PFN_vkDestroyPipeline>(get(device,"vkDestroyPipeline"));
    const auto create_set=reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(get(device,"vkCreateDescriptorSetLayout"));const auto create_layout=reinterpret_cast<PFN_vkCreatePipelineLayout>(get(device,"vkCreatePipelineLayout"));const auto create_shader=reinterpret_cast<PFN_vkCreateShaderModule>(get(device,"vkCreateShaderModule"));const auto create_pipeline=reinterpret_cast<PFN_vkCreateComputePipelines>(get(device,"vkCreateComputePipelines"));
    require(p.destroy_set&&p.destroy_layout&&p.destroy_shader&&p.destroy_pipeline&&create_set&&create_layout&&create_shader&&create_pipeline,"MToon composite program driver entries");
    std::array<VkDescriptorSetLayoutBinding,8> bindings{};for(unsigned i=0;i<8;++i)bindings[i]={i,i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:(i<4||i==7)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=8;si.pBindings=bindings.data();checked(create_set(device,&si,nullptr,&p.dsl),"MToon composite set layout");require(bool(p.dsl),"MToon composite null set layout");VkPipelineLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&p.dsl;checked(create_layout(device,&li,nullptr,&p.layout),"MToon composite layout");require(bool(p.layout),"MToon composite null layout");
    VkShaderModuleCreateInfo mi{};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.codeSize=shader.count*4;mi.pCode=shader.words;checked(create_shader(device,&mi,nullptr,&p.shader),"MToon composite shader");require(bool(p.shader),"MToon composite null shader");VkComputePipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;ci.layout=p.layout;ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=p.shader;ci.stage.pName="cs_composite";checked(create_pipeline(device,VK_NULL_HANDLE,1,&ci,nullptr,&p.pipeline),"MToon composite pipeline");require(bool(p.pipeline),"MToon composite null pipeline");
}
NativeMToonCompositeProgram::~NativeMToonCompositeProgram()=default;
void NativeMToonCompositeProgram::quarantine()const noexcept{if(impl)impl->uncertain=true;}
bool NativeMToonCompositeProgram::reusable()const noexcept{return impl&&!impl->uncertain;}
struct NativeMToonCompositePass::Impl {
    VkDevice device{};const bool* parent=nullptr;bool uncertain=false,recorded=false;NativeMToonCompositeParameters parameters;
    std::shared_ptr<const NativeMToonCompositeProgram> program;std::atomic<bool>* program_uncertain=nullptr;
    std::shared_ptr<CompositeBank> bank;VkBuffer buffer{};void* mapped=nullptr;VkDeviceSize observation_offset=0;VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};VkDescriptorPool pool{};VkDescriptorSet set{};
#define DECLARE(n) PFN_##n n{};
    COMPOSITE_FUNCTIONS(DECLARE)
#undef DECLARE
    bool quarantined()const noexcept{return uncertain||(parent&&*parent)||(program_uncertain&&program_uncertain->load());}
    ~Impl(){if(quarantined()){if(program_uncertain)program_uncertain->store(true);if(bank)bank->uncertain=true;}}
};
NativeMToonCompositePass::NativeMToonCompositePass(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonCompositeCapabilities& caps,
    NativeMToonCompositeParameters parameters,VkDescriptorBufferInfo packets,VkDescriptorBufferInfo identities,VkDescriptorBufferInfo permit,
    const std::array<NativeMToonCompositeImage,3>& images,NativeMToonCompositeShader shader,const bool* quarantine,std::shared_ptr<const NativeMToonCompositeProgram> prepared){
    const auto& l=caps.limits;const auto& m=caps.memory;const auto pixels=std::uint64_t(parameters.width)*parameters.height;
    require(device&&get&&quarantine&&!*quarantine&&parameters.width&&parameters.height&&parameters.width<=16384&&parameters.height<=16384&&
        pixels<=pixel_limit&&parameters.generation&&parameters.reserved<=1&&caps.storage_image_extended_formats,"MToon composite inputs/features");
    require(l.maxBoundDescriptorSets&&l.maxUniformBufferRange>=16&&l.maxDescriptorSetUniformBuffers>=2&&l.maxPerStageDescriptorUniformBuffers>=2&&
        l.maxDescriptorSetStorageBuffers>=3&&l.maxPerStageDescriptorStorageBuffers>=3&&l.maxDescriptorSetStorageImages>=3&&l.maxPerStageDescriptorStorageImages>=3&&l.maxPerStageResources>=8&&
        l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=8&&l.maxComputeWorkGroupSize[1]>=8&&l.maxComputeWorkGroupSize[2]&&
        l.maxComputeWorkGroupCount[0]>=(parameters.width+7u)/8u&&l.maxComputeWorkGroupCount[1]>=(parameters.height+7u)/8u&&l.maxComputeWorkGroupCount[2],"MToon composite limits");
    require(span(packets,16+pixels*16,l.maxStorageBufferRange,l.minStorageBufferOffsetAlignment)&&span(identities,pixels*4,l.maxStorageBufferRange,l.minStorageBufferOffsetAlignment)&&
        span(permit,16,l.maxUniformBufferRange,l.minUniformBufferOffsetAlignment),"MToon composite input spans");
    packets.range=16+pixels*16;identities.range=pixels*4;permit.range=16;
    require(!overlap(packets,identities)&&!overlap(packets,permit)&&!overlap(identities,permit),"MToon composite input overlap");
    constexpr std::array<VkFormat,3> formats{VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16_SFLOAT,VK_FORMAT_R8_UNORM};
    for(unsigned i=0;i<3;++i){const auto& a=images[i];require(a.image&&a.descriptor.imageView&&!a.descriptor.sampler&&a.descriptor.imageLayout==VK_IMAGE_LAYOUT_GENERAL&&a.format==formats[i]&&
        a.extent.width==parameters.width&&a.extent.height==parameters.height&&(caps.format_features[i]&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT),"MToon composite destination");
        for(unsigned j=0;j<i;++j)require(a.image!=images[j].image&&a.descriptor.imageView!=images[j].descriptor.imageView,"MToon composite destination alias");}
    require(!prepared||(prepared->impl&&prepared->impl->device==device&&prepared->impl->parent==quarantine&&!prepared->impl->uncertain&&!shader.words&&!shader.count),"MToon composite prepared-program owner");
    require((prepared||(shader.words&&shader.count>=5&&shader.count<=262144&&shader.words[0]==0x07230203))&&m.memoryTypeCount&&m.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&m.memoryHeapCount&&m.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"MToon composite shader/memory");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.parent=quarantine;p.parameters=parameters;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"MToon composite missing driver entry");
    COMPOSITE_FUNCTIONS(LOAD)
#undef LOAD
    require(power(l.minStorageBufferOffsetAlignment)&&l.minStorageBufferOffsetAlignment<=32768,"MToon observation alignment");
    p.observation_offset=l.minStorageBufferOffsetAlignment>16?l.minStorageBufferOffsetAlignment:16;
    p.program=prepared?std::move(prepared):std::make_shared<NativeMToonCompositeProgram>(device,get,caps,shader,quarantine);
    p.program_uncertain=&p.program->impl->uncertain;
    p.dsl=p.program->impl->dsl;p.layout=p.program->impl->layout;p.shader=p.program->impl->shader;p.pipeline=p.program->impl->pipeline;
    auto& banks=p.program->impl->banks;
    for(const auto& bank:banks)if(bank.use_count()==1&&!bank->uncertain&&bank->observation_offset==p.observation_offset){p.bank=bank;break;}
    if(!p.bank){
        auto bank=std::make_shared<CompositeBank>();bank->device=device;bank->observation_offset=p.observation_offset;
        bank->destroy_pool=p.vkDestroyDescriptorPool;bank->unmap=p.vkUnmapMemory;bank->destroy_buffer=p.vkDestroyBuffer;bank->free_memory=p.vkFreeMemory;
        const auto buffer_bytes=p.observation_offset+32;
        VkBufferCreateInfo bi{};bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bi.size=buffer_bytes;bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        checked(p.vkCreateBuffer(device,&bi,nullptr,&bank->buffer),"MToon composite parameter buffer");require(bool(bank->buffer),"MToon composite null buffer");
        VkMemoryRequirements r{};p.vkGetBufferMemoryRequirements(device,bank->buffer,&r);require(r.size>=buffer_bytes&&r.size<=65536&&power(r.alignment)&&r.memoryTypeBits,"MToon composite parameter requirements");
        unsigned type=m.memoryTypeCount;for(unsigned i=0;i<m.memoryTypeCount;++i)if((r.memoryTypeBits&(1u<<i))&&(m.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&m.memoryTypes[i].heapIndex<m.memoryHeapCount&&m.memoryHeaps[m.memoryTypes[i].heapIndex].size>=r.size){type=i;break;}require(type<m.memoryTypeCount,"MToon composite coherent parameter type");
        VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=r.size;ai.memoryTypeIndex=type;
        checked(p.vkAllocateMemory(device,&ai,nullptr,&bank->memory),"MToon composite memory");require(bool(bank->memory),"MToon composite null memory");
        checked(p.vkBindBufferMemory(device,bank->buffer,bank->memory,0),"MToon composite binding");
        checked(p.vkMapMemory(device,bank->memory,0,buffer_bytes,0,&bank->mapped),"MToon composite mapping");require(bank->mapped!=nullptr,"MToon composite null mapping");
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,3}};
        VkDescriptorPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pi.maxSets=1;pi.poolSizeCount=3;pi.pPoolSizes=sizes;
        checked(p.vkCreateDescriptorPool(device,&pi,nullptr,&bank->pool),"MToon composite pool");require(bool(bank->pool),"MToon composite null pool");
        VkDescriptorSetAllocateInfo di{};di.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;di.descriptorPool=bank->pool;di.descriptorSetCount=1;di.pSetLayouts=&p.dsl;
        checked(p.vkAllocateDescriptorSets(device,&di,&bank->set),"MToon composite set");require(bool(bank->set),"MToon composite null set");
        // Three retained recording banks; overflow remains an ordinary owned
        // allocation rather than changing valid caller admission.
        if(banks.size()<3)banks.push_back(bank);p.bank=std::move(bank);
    }
    p.buffer=p.bank->buffer;p.mapped=p.bank->mapped;p.pool=p.bank->pool;p.set=p.bank->set;
    std::memcpy(p.mapped,&parameters,16);
    const std::array<std::uint32_t,8> observations{0x4d544141u,parameters.generation,parameters.width,parameters.height,0,0,0,0};
    std::memcpy(static_cast<unsigned char*>(p.mapped)+p.observation_offset,observations.data(),32);
    std::array<VkDescriptorSetLayoutBinding,8> bindings{};for(unsigned i=0;i<8;++i)bindings[i]={i,i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:(i<4||i==7)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    const VkDescriptorBufferInfo buffers[]{{p.buffer,0,16},permit,packets,identities,{p.buffer,p.observation_offset,32}};std::array<VkWriteDescriptorSet,8> writes{};for(unsigned i=0;i<8;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=p.set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=bindings[i].descriptorType;if(i<4||i==7)w.pBufferInfo=&buffers[i==7?4:i];else w.pImageInfo=&images[i-4].descriptor;}p.vkUpdateDescriptorSets(device,8,writes.data(),0,nullptr);
}
NativeMToonCompositePass::~NativeMToonCompositePass()=default;
bool NativeMToonCompositePass::record(VkCommandBuffer command,VkPipeline original,VkPipelineLayout compatible,std::uint32_t count,const VkDescriptorSet* sets)noexcept{
    if(!impl||impl->recorded||impl->quarantined()||!command||!original||!compatible||!sets||!count||count>32)return false;for(unsigned i=0;i<count;++i)if(!sets[i])return false;
    auto& p=*impl;p.recorded=true;VkMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&p.set,0,nullptr);p.vkCmdDispatch(command,(p.parameters.width+7u)/8u,(p.parameters.height+7u)/8u,1);
    VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=p.buffer;host.offset=p.observation_offset;host.size=32;
    p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;p.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,original);p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,compatible,0,count,sets,0,nullptr);return true;
}
std::shared_ptr<const NativeMToonCompositeProgram> NativeMToonCompositePass::prepared_program()const noexcept{return impl&&!impl->quarantined()?impl->program:std::shared_ptr<const NativeMToonCompositeProgram>{};}
NativeMToonCompositeObservation NativeMToonCompositePass::observation_after_original_completion()const noexcept{
    NativeMToonCompositeObservation out;if(!impl||!impl->recorded||impl->quarantined()||!impl->mapped)return out;
    const auto& p=*impl;std::array<std::uint32_t,8> values{};std::memcpy(values.data(),static_cast<const unsigned char*>(p.mapped)+p.observation_offset,32);
    if(values[0]!=0x4d544141u||values[1]!=p.parameters.generation||values[2]!=p.parameters.width||values[3]!=p.parameters.height||
       values[7]>values[6]||values[6]>values[5]||values[5]>values[4]||values[4]>16641u)return out;
    out.available=true;out.generation=values[1];out.sampled=values[4];out.eligible=values[5];out.mixed=values[6];out.outline_mixed=values[7];return out;
}
void NativeMToonCompositePass::quarantine()noexcept{if(impl){impl->uncertain=true;if(impl->bank)impl->bank->uncertain=true;if(impl->program)impl->program->impl->uncertain=true;}}
#undef COMPOSITE_FUNCTIONS
}
