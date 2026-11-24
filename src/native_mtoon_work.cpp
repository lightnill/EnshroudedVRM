#include "native_mtoon_work.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
namespace evrm {namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void checked(VkResult result,const char* why){require(result==VK_SUCCESS,why);}
bool power_two(VkDeviceSize n){return n&&!(n&(n-1));}
}
#define MTOON_PROGRAM_FUNCTIONS(X) \
X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
X(vkCreateComputePipelines) X(vkDestroyPipeline)
struct NativeMToonWorkProgram::Impl {
    VkDevice device{};const bool* parent=nullptr;std::atomic<bool> uncertain{false};
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};std::array<VkShaderModule,4> shaders{};std::array<VkPipeline,4> pipelines{};
#define DECLARE(n) PFN_##n n{};
    MTOON_PROGRAM_FUNCTIONS(DECLARE)
#undef DECLARE
    ~Impl(){if(uncertain)return;for(auto p:pipelines)if(p)vkDestroyPipeline(device,p,nullptr);for(auto s:shaders)if(s)vkDestroyShaderModule(device,s,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(dsl)vkDestroyDescriptorSetLayout(device,dsl,nullptr);}
};
NativeMToonWorkProgram::NativeMToonWorkProgram(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonWorkCapabilities& caps,
    const std::array<NativeMToonWorkShader,4>& shaders,const bool* parent):impl(std::make_unique<Impl>()){
    require(device&&get&&parent,"MToon work program owner");const auto& l=caps.limits;
    require(l.maxBoundDescriptorSets&&l.maxUniformBufferRange>=16&&l.maxDescriptorSetUniformBuffers&&l.maxPerStageDescriptorUniformBuffers&&
        l.maxDescriptorSetStorageBuffers>=4&&l.maxPerStageDescriptorStorageBuffers>=4&&l.maxPerStageResources>=5&&
        l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=64&&l.maxComputeWorkGroupSize[1]>=8&&
        l.maxComputeWorkGroupSize[2]&&l.maxComputeSharedMemorySize>=4,"MToon work program limits");
    for(const auto& code:shaders)require(code.words&&code.count>=5&&code.count<=262144&&code.words[0]==0x07230203,"MToon work program shader");
    auto& p=*impl;p.device=device;p.parent=parent;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"MToon work program driver entry");
    MTOON_PROGRAM_FUNCTIONS(LOAD)
#undef LOAD
    std::array<VkDescriptorSetLayoutBinding,5> bindings{};for(unsigned i=0;i<5;++i)bindings[i]={i,i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo si{};si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;si.bindingCount=5;si.pBindings=bindings.data();checked(p.vkCreateDescriptorSetLayout(device,&si,nullptr,&p.dsl),"MToon work set layout");require(bool(p.dsl),"MToon work null set layout");
    VkPipelineLayoutCreateInfo li{};li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;li.setLayoutCount=1;li.pSetLayouts=&p.dsl;checked(p.vkCreatePipelineLayout(device,&li,nullptr,&p.layout),"MToon work pipeline layout");require(bool(p.layout),"MToon work null pipeline layout");
    constexpr const char* entries[]{"cs_init","cs_bounds","cs_arguments","cs_finish"};
    for(unsigned i=0;i<4;++i){VkShaderModuleCreateInfo mi{};mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;mi.codeSize=shaders[i].count*4;mi.pCode=shaders[i].words;
        checked(p.vkCreateShaderModule(device,&mi,nullptr,&p.shaders[i]),"MToon work module");require(bool(p.shaders[i]),"MToon work null module");VkComputePipelineCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;ci.layout=p.layout;ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=p.shaders[i];ci.stage.pName=entries[i];
        checked(p.vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&p.pipelines[i]),"MToon work pipeline");require(bool(p.pipelines[i]),"MToon work null pipeline");}
}
NativeMToonWorkProgram::~NativeMToonWorkProgram()=default;
void NativeMToonWorkProgram::quarantine()const noexcept{if(impl)impl->uncertain=true;}
bool NativeMToonWorkProgram::reusable()const noexcept{return impl&&!impl->uncertain;}
#define MTOON_WORK_FUNCTIONS(X) \
X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
X(vkCmdFillBuffer) X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDispatch)
// Driver objects only. Destroyed with its last holder by the same calls, in the
// same order, as a work object made before: the pool, then per buffer the
// unmap, the buffer and its memory.
class NativeMToonWorkStorage {
public:
    VkDevice device{};const bool* parent_quarantine=nullptr;bool uncertain=false,in_use=false;
    std::shared_ptr<const NativeMToonWorkProgram> program;std::atomic<bool>* program_uncertain=nullptr;
    VkDescriptorSetLayout dsl{};VkDeviceSize allocated=0;
    struct Buffer {VkBuffer handle{};VkDeviceMemory memory{};VkDeviceSize bytes=0;void* mapped=nullptr;};
    std::array<Buffer,4> buffers; // ranges,records,summary,parameters
    VkDescriptorPool pool{};VkDescriptorSet set{};
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool{};PFN_vkUnmapMemory vkUnmapMemory{};PFN_vkDestroyBuffer vkDestroyBuffer{};PFN_vkFreeMemory vkFreeMemory{};
    bool quarantined()const noexcept{return uncertain||(parent_quarantine&&*parent_quarantine)||(program_uncertain&&program_uncertain->load());}
    ~NativeMToonWorkStorage(){if(quarantined()){if(program_uncertain)program_uncertain->store(true);return;}
        if(pool&&vkDestroyDescriptorPool)vkDestroyDescriptorPool(device,pool,nullptr);
        for(auto& b:buffers){if(b.mapped)vkUnmapMemory(device,b.memory);if(b.handle)vkDestroyBuffer(device,b.handle,nullptr);if(b.memory)vkFreeMemory(device,b.memory,nullptr);}
    }
};
void native_mtoon_work_storage_quarantine(const std::shared_ptr<NativeMToonWorkStorage>& storage)noexcept{if(storage)storage->uncertain=true;}
VkDeviceSize native_mtoon_work_storage_bytes(const std::shared_ptr<NativeMToonWorkStorage>& storage)noexcept{return storage?storage->allocated:0;}
struct NativeMToonWork::Impl {
    VkDevice device{};const bool* parent_quarantine=nullptr;bool uncertain=false,recorded=false,failed=false;
    std::shared_ptr<const NativeMToonWorkProgram> program;std::atomic<bool>* program_uncertain=nullptr;
    NativeMToonWorkParameters parameters;VkPhysicalDeviceMemoryProperties memory{};
    // Private to this object, or shared with the caller's retained slot.
    std::shared_ptr<NativeMToonWorkStorage> storage;
    VkDescriptorSetLayout dsl{};VkPipelineLayout layout{};
    std::array<VkPipeline,4> pipelines{};
#define DECLARE(n) PFN_##n n{};
    MTOON_WORK_FUNCTIONS(DECLARE)
#undef DECLARE
    bool quarantined()const noexcept{return uncertain||(parent_quarantine&&*parent_quarantine)||(program_uncertain&&program_uncertain->load());}
    // Uncertain completion marks the storage, so neither this object nor a
    // retained slot ever destroys or reissues it. Otherwise private storage is
    // destroyed here with its last reference, and slot storage becomes free.
    ~Impl(){if(quarantined()){if(program_uncertain)program_uncertain->store(true);if(storage)storage->uncertain=true;return;}
        if(storage)storage->in_use=false;
    }
    void buffer(unsigned index,VkDeviceSize bytes,VkBufferUsageFlags usage,bool host){auto& allocated=storage->allocated;auto& b=storage->buffers[index];b.bytes=bytes;
        VkBufferCreateInfo ci{};ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;ci.size=bytes;ci.usage=usage;
        checked(vkCreateBuffer(device,&ci,nullptr,&b.handle),"MToon work buffer");require(bool(b.handle),"MToon work null buffer");
        VkMemoryRequirements r{};vkGetBufferMemoryRequirements(device,b.handle,&r);
        require(r.size>=bytes&&power_two(r.alignment)&&r.alignment<=memory_byte_limit&&r.size<=memory_byte_limit-allocated,"MToon work allocation budget/alignment");
        unsigned type=memory.memoryTypeCount;for(unsigned i=0;i<memory.memoryTypeCount;++i){const auto& t=memory.memoryTypes[i];
            const VkMemoryPropertyFlags needed=host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            if((r.memoryTypeBits&(1u<<i))&&(t.propertyFlags&needed)==needed&&!(t.propertyFlags&(VK_MEMORY_PROPERTY_PROTECTED_BIT|VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT))&&
                t.heapIndex<memory.memoryHeapCount&&memory.memoryHeaps[t.heapIndex].size>=r.size){type=i;break;}}
        require(type<memory.memoryTypeCount,"MToon work memory type");VkMemoryAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.allocationSize=r.size;ai.memoryTypeIndex=type;
        checked(vkAllocateMemory(device,&ai,nullptr,&b.memory),"MToon work memory");require(bool(b.memory),"MToon work null memory");allocated+=r.size;
        checked(vkBindBufferMemory(device,b.handle,b.memory,0),"MToon work binding");
        if(host){checked(vkMapMemory(device,b.memory,0,bytes,0,&b.mapped),"MToon work map");require(b.mapped!=nullptr,"MToon work null map");std::memset(b.mapped,0,std::size_t(bytes));}
    }
};
NativeMToonWork::NativeMToonWork(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonWorkCapabilities& caps,VkDescriptorBufferInfo identities,
    NativeMToonWorkParameters parameters,const std::vector<NativeMToonWorkDraw>& draws,const std::array<NativeMToonWorkShader,4>& shaders,const bool* quarantine,std::shared_ptr<const NativeMToonWorkProgram> prepared,
    std::shared_ptr<NativeMToonWorkStorage>* retained_slot){
    const auto& l=caps.limits;const auto& m=caps.memory;const auto pixels=std::uint64_t(parameters.width)*parameters.height;
    require(device&&get&&quarantine&&!*quarantine&&parameters.width&&parameters.height&&parameters.width<=16384&&parameters.height<=16384&&pixels<=pixel_limit&&
        parameters.draws&&parameters.draws<=draw_limit&&parameters.draws==draws.size()&&parameters.budget&&parameters.budget<=group_budget_limit,"MToon work inputs");
    require(m.memoryTypeCount&&m.memoryTypeCount<=VK_MAX_MEMORY_TYPES&&m.memoryHeapCount&&m.memoryHeapCount<=VK_MAX_MEMORY_HEAPS,"MToon work memory metadata");
    require(l.maxBoundDescriptorSets&&l.maxUniformBufferRange>=16&&l.maxDescriptorSetUniformBuffers&&l.maxPerStageDescriptorUniformBuffers&&
        l.maxDescriptorSetStorageBuffers>=4&&l.maxPerStageDescriptorStorageBuffers>=4&&l.maxPerStageResources>=5&&
        l.maxStorageBufferRange>=pixels*4&&l.maxStorageBufferRange>=draws.size()*32&&l.maxComputeWorkGroupInvocations>=64&&l.maxComputeWorkGroupSize[0]>=64&&
        l.maxComputeWorkGroupSize[1]>=8&&l.maxComputeWorkGroupSize[2]&&l.maxComputeSharedMemorySize>=4&&l.maxComputeWorkGroupCount[0]>=(parameters.width+7u)/8u&&
        l.maxComputeWorkGroupCount[0]>=(parameters.draws+63u)/64u&&l.maxComputeWorkGroupCount[1]>=(parameters.height+7u)/8u&&l.maxComputeWorkGroupCount[2],"MToon work device limits");
    require(identities.buffer&&identities.range!=VK_WHOLE_SIZE&&identities.range>=pixels*4&&identities.range<=l.maxStorageBufferRange&&
        power_two(l.minStorageBufferOffsetAlignment)&&identities.offset%l.minStorageBufferOffsetAlignment==0&&identities.offset<=~VkDeviceSize(0)-identities.range,"MToon work identity span");
    std::uint32_t end=0;bool any=false;for(const auto& d:draws){require(d.count&&d.count<=0x7fffffffu&&d.first<=0x7fffffffu-d.count&&d.first>=end&&d.selected<=1&&!d.reserved,"MToon work draw intervals");end=d.first+d.count;any=any||d.selected!=0;}
    require(any,"MToon work has no selected materials");
    require(!prepared||(prepared->impl&&prepared->impl->device==device&&prepared->impl->parent==quarantine&&prepared->reusable()),"MToon work prepared-program owner");
    for(const auto& code:shaders)require(prepared?(!code.words&&!code.count):(code.words&&code.count>=5&&code.count<=262144&&code.words[0]==0x07230203),"MToon work shader/prepared ambiguity");
    impl=std::make_unique<Impl>();auto& p=*impl;p.device=device;p.parent_quarantine=quarantine;p.memory=m;p.parameters=parameters;
#define LOAD(n) p.n=reinterpret_cast<PFN_##n>(get(device,#n));require(p.n!=nullptr,"MToon work missing driver entry");
    MTOON_WORK_FUNCTIONS(LOAD)
#undef LOAD
    p.program=prepared?std::move(prepared):std::make_shared<NativeMToonWorkProgram>(device,get,caps,shaders,quarantine);
    p.program_uncertain=&p.program->impl->uncertain;p.dsl=p.program->impl->dsl;p.layout=p.program->impl->layout;p.pipelines=p.program->impl->pipelines;
    const std::array<VkDeviceSize,4> bytes{VkDeviceSize(draws.size())*16,VkDeviceSize(draws.size())*32,16,16};
    // Exact reuse only: the retained storage is what the calls below would
    // create. Its previous work object has retired (the slot owner's rule);
    // one that is still marked in use is never touched.
    if(retained_slot&&*retained_slot){const auto& kept=**retained_slot;
        bool same=!kept.in_use&&!kept.quarantined()&&kept.device==device&&kept.parent_quarantine==quarantine&&kept.program==p.program&&kept.dsl==p.dsl&&kept.pool&&kept.set;
        for(unsigned i=0;i<4;++i)same=same&&kept.buffers[i].handle&&kept.buffers[i].bytes==bytes[i];
        if(same)p.storage=*retained_slot;
    }
    if(!p.storage){
        p.storage=std::make_shared<NativeMToonWorkStorage>();auto& s=*p.storage;s.device=device;s.parent_quarantine=quarantine;s.program=p.program;s.program_uncertain=p.program_uncertain;s.dsl=p.dsl;
        s.vkDestroyDescriptorPool=p.vkDestroyDescriptorPool;s.vkUnmapMemory=p.vkUnmapMemory;s.vkDestroyBuffer=p.vkDestroyBuffer;s.vkFreeMemory=p.vkFreeMemory;
        p.buffer(0,bytes[0],VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);
        p.buffer(1,bytes[1],VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false);
        p.buffer(2,bytes[2],VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT,false);
        p.buffer(3,bytes[3],VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true);
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};VkDescriptorPoolCreateInfo pi{};pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pi.maxSets=1;pi.poolSizeCount=2;pi.pPoolSizes=sizes;checked(p.vkCreateDescriptorPool(device,&pi,nullptr,&s.pool),"MToon work descriptor pool");require(bool(s.pool),"MToon work null pool");
        VkDescriptorSetAllocateInfo ai{};ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;ai.descriptorPool=s.pool;ai.descriptorSetCount=1;ai.pSetLayouts=&p.dsl;checked(p.vkAllocateDescriptorSets(device,&ai,&s.set),"MToon work descriptors");require(bool(s.set),"MToon work null descriptors");
        // The slot takes complete storage only. What it held has no live work
        // object and is destroyed here with its last reference.
        if(retained_slot&&(!*retained_slot||!(*retained_slot)->in_use))*retained_slot=p.storage;
    }
    auto& held=*p.storage;held.in_use=true;
    // Both mapped buffers are written in full for every work object.
    std::memcpy(held.buffers[0].mapped,draws.data(),std::size_t(bytes[0]));std::memcpy(held.buffers[3].mapped,&parameters,16);
    identities.range=pixels*4;std::array<VkDescriptorBufferInfo,5> infos{identities};for(unsigned i=0;i<4;++i)infos[i+1]={held.buffers[i].handle,0,held.buffers[i].bytes};
    std::array<VkWriteDescriptorSet,5> writes{};for(unsigned i=0;i<5;++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=held.set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w.pBufferInfo=&infos[i];}
    p.vkUpdateDescriptorSets(device,5,writes.data(),0,nullptr);
}
NativeMToonWork::~NativeMToonWork()=default;
bool NativeMToonWork::record(VkCommandBuffer command)noexcept{
    if(!impl||impl->recorded||impl->failed||impl->quarantined())return false;auto& p=*impl;if(!command){p.failed=true;return false;}p.recorded=true;
    const auto& s=*p.storage;
    p.vkCmdFillBuffer(command,s.buffers[2].handle,0,16,0);
    const auto barrier=[&](VkPipelineStageFlags from,VkPipelineStageFlags to,VkAccessFlags src,VkAccessFlags dst){VkMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;b.srcAccessMask=src;b.dstAccessMask=dst;p.vkCmdPipelineBarrier(command,from,to,0,1,&b,0,nullptr,0,nullptr);};
    barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_UNIFORM_READ_BIT);
    p.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&s.set,0,nullptr);
    const auto count=(p.parameters.draws+63u)/64u;
    for(unsigned i=0;i<4;++i){p.vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipelines[i]);p.vkCmdDispatch(command,i==1?(p.parameters.width+7u)/8u:count,i==1?(p.parameters.height+7u)/8u:1u,1);
        if(i!=3)barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);}
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT);return true;
}
VkDescriptorBufferInfo NativeMToonWork::records()const noexcept{return impl&&impl->recorded&&!impl->failed&&!impl->quarantined()?VkDescriptorBufferInfo{impl->storage->buffers[1].handle,0,impl->storage->buffers[1].bytes}:VkDescriptorBufferInfo{};}
VkDescriptorBufferInfo NativeMToonWork::summary()const noexcept{return impl&&impl->recorded&&!impl->failed&&!impl->quarantined()?VkDescriptorBufferInfo{impl->storage->buffers[2].handle,0,16}:VkDescriptorBufferInfo{};}
VkDeviceSize NativeMToonWork::indirect_offset(std::uint32_t draw)const noexcept{return impl&&draw<impl->parameters.draws?VkDeviceSize(draw)*32+16:VK_WHOLE_SIZE;}
std::shared_ptr<const NativeMToonWorkProgram> NativeMToonWork::prepared_program()const noexcept{return impl&&!impl->quarantined()?impl->program:std::shared_ptr<const NativeMToonWorkProgram>{};}
void NativeMToonWork::quarantine()noexcept{if(impl){impl->uncertain=true;if(impl->program)impl->program->quarantine();}}
#undef MTOON_WORK_FUNCTIONS
#undef MTOON_PROGRAM_FUNCTIONS
}
